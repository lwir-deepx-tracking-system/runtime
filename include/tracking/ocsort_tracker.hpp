#pragma once

#include <array>
#include <deque>
#include <vector>

#include "config/app_config.hpp"
#include "tracking/camera_motion.hpp"
#include "tracking/tracker.hpp"

// OC-SORT (Observation-Centric SORT, Cao et al., CVPR 2023) 단독 Tracker.
// 공식 구현(github.com/noahcao/OC_SORT, trackers/ocsort_tracker)을 그대로 옮겼고, 같은 입력에 같은 결과를 내도록
// 공식 Python 코드와 대조해 검증했다. ByteTrackTracker와 코드를 공유하지 않으므로 두 알고리즘을 따로 비교할 수 있다.
//
// ByteTrack과 다른 점 (공식 구현 그대로)
//   - Kalman: SORT 방식 7차원 상태 [cx, cy, s(넓이), r(가로세로비), vcx, vcy, vs]. 노이즈가 박스 크기에 비례하지 않는 고정값
//   - 매칭: 1차(high 검출 ↔ 모든 Track, IoU + OCM 방향 항) → [use_byte면 low 검출] → OCR(마지막 관측 박스 ↔ 남은 high)
//     매칭은 IoU iou_threshold 이상이어야 하고, 헝가리안은 매칭 후보를 미리 거르지 않고 전체 행렬로 짝을 정한다
//   - ORU: 놓쳤던 Track을 다시 찾으면 놓치기 직전 Kalman 상태로 되돌려 직선 보간한 가상 관측으로 다시 계산한다
//   - 출력: 이번 프레임에 매칭된 Track만, 연속 매칭 min_hits 이상(처음 min_hits 프레임은 예외)일 때 내고,
//           박스는 Kalman 추정이 아니라 마지막으로 매칭된 검출 박스다
//   - 새 Track: 매칭되지 않은 high 검출은 모두 새 Track이 된다 (별도 새 ID 임계값 없음)
//
// 공식 구현에 없는 것 (모두 기본 꺼짐. 꺼져 있으면 공식 구현과 출력이 같다)
//   - 카메라 움직임 보정(CMC): set_camera_motion()을 부르면 Kalman 상태, ORU 저장 상태, 과거 관측 박스를
//     모두 현재 화면 좌표로 옮긴다.
//   - Buffered IoU (buffer_scale > 0): OCR 뒤에 남은 Track의 예측 박스와 남은 high 검출을 둘 다 넓혀
//     한 번 더 매칭한다. 작은 박스에서 예측이 몇 px 빗나가 IoU가 0이 되는 경우를 구제한다.
//   - 놓친 프레임 출력 (lost_output_frames > 0): 출력된 적 있는 Track이 매칭을 놓치면 그 뒤 이 프레임 수까지
//     Kalman 예측 박스로 같은 ID를 계속 낸다. 이 박스는 검출이 아니라 예측이다.
namespace ocsort
{
using Vec7 = std::array<double, 7>;
using Mat7 = std::array<std::array<double, 7>, 7>;
using Box5 = std::array<double, 5>;  // x1, y1, x2, y2, score (공식 구현과 같은 형식)

struct Observation
{
    int age = 0;  // 이 관측이 들어온 때의 Track age
    Box5 box{};
};

// Track 하나 (공식 KalmanBoxTracker + KalmanFilterNew의 ORU 상태)
struct OCTrack
{
    int id = 0;  // 출력 ID는 id + 1

    // Kalman (SORT)
    Vec7 x{};
    Mat7 P{};

    // ORU (공식 freeze/unfreeze)
    bool observed = false;       // 직전 Kalman update가 관측이었는지
    bool frozen = false;         // 놓친 첫 프레임에 저장한 상태가 있는지
    Vec7 saved_x{};
    Mat7 saved_P{};
    std::array<double, 4> last_z{};  // 마지막으로 update한 측정값 [cx, cy, s, r] (보간 시작점)
    int misses_since_z = 0;          // 그 뒤로 관측 없이 지난 프레임 수

    int time_since_update = 0;
    int hits = 0;
    int hit_streak = 0;
    int age = 0;
    int class_id = 0;
    double score = 0.0;  // 마지막으로 매칭된(또는 생성한) 검출 점수 (출력 confidence)

    bool has_last_observation = false;
    Box5 last_observation{-1.0, -1.0, -1.0, -1.0, -1.0};
    std::deque<Observation> observations;  // OCM에 필요한 최근 delta_t 범위만 남긴다

    bool reported = false;  // 한 번이라도 출력됐는지 (놓친 프레임 출력 대상 판단)

    bool has_velocity = false;
    double velocity_y = 0.0;  // 단위 방향 벡터 (공식 구현과 같은 (dy, dx) 순서)
    double velocity_x = 0.0;
};
}  // namespace ocsort


class OCSortTracker : public Tracker
{
public:
    explicit OCSortTracker(const OCSortConfig& config);
    ~OCSortTracker() override = default;

    // Detection 목록으로 OC-SORT Tracking을 수행한다. 검출이 없는 프레임에도 매 프레임 호출해야 한다.
    std::vector<Track> track(const std::vector<Detection>& detections) override;

    // 내부 상태를 초기화한다. ID는 1부터 다시 시작한다.
    void reset() override;

    // 다음 track() 호출에 적용할 카메라 움직임(직전 프레임 → 현재 프레임). track()이 끝나면 지워진다.
    void set_camera_motion(const CameraMotion& motion) { camera_motion_ = motion; }

    const OCSortConfig& config() const { return config_; }

private:
    OCSortConfig config_;
    std::vector<ocsort::OCTrack> trackers_;
    int frame_count_ = 0;
    int next_id_ = 0;
    CameraMotion camera_motion_;

    ocsort::OCTrack create_track(const ocsort::Box5& det, int class_id);
    void predict(ocsort::OCTrack& t) const;
    // det가 nullptr이면 이번 프레임에 관측 없음
    void update(ocsort::OCTrack& t, const ocsort::Box5* det, int class_id) const;
    void apply_motion(ocsort::OCTrack& t, const CameraMotion& m) const;
};
