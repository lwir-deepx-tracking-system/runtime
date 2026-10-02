#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "tracking/kalman_filter.hpp"
#include "tracking/tracker.hpp"


// ByteTrack 파라미터. 기본값은 공식 ByteTrack 구현 기준이다.
struct ByteTrackConfig
{
    float track_thresh = 0.5f;      // 이 점수 이상: high detection (1차 매칭)
    float low_thresh = 0.1f;        // 이 점수 초과 ~ track_thresh 미만: low detection (2차 매칭)
    float new_track_thresh = 0.6f;  // 새 Track을 만드는 최소 점수
    float match_thresh = 0.8f;      // 1차 매칭에서 허용하는 최대 IoU distance (1 - IoU)
    int track_buffer = 30;          // 놓친 Track을 유지하는 프레임 수 (30fps 기준)
    int frame_rate = 30;
    bool fuse_score = false;  // true: 1차·미확정 매칭 비용에 검출 점수를 곱함 (공식 ByteTrack MOT17 설정)
};

// ByteTrack 설정 YAML을 읽고 값의 범위를 검증한다.
// 파일이 없거나, 필수 키가 빠졌거나, 모르는 키·잘못된 값이 있으면 std::runtime_error.
//   필수: track_threshold, match_threshold, track_buffer
//   선택: low_threshold(기본 0.1), new_track_threshold(기본 track_threshold + 0.1),
//         frame_rate(기본 30)
ByteTrackConfig load_bytetrack_config(const std::string& path);


namespace bytetrack
{
enum class TrackState
{
    New,
    Tracked,
    Lost,
    Removed
};

// Tracker 내부에서 관리하는 Track 하나의 상태
struct STrack
{
    int track_id = 0;
    TrackState state = TrackState::New;
    bool is_activated = false;

    KalmanState kf{};
    std::array<float, 4> det_tlwh{};  // 검출 bbox (x, y, width, height)

    float score = 0.0f;
    int class_id = -1;

    int frame_id = 0;       // 마지막으로 검출과 매칭된 프레임
    int start_frame = 0;
    int tracklet_len = 0;
};

using STrackPtr = std::shared_ptr<STrack>;
}  // namespace bytetrack


// ByteTrack 알고리즘을 이용해 Detection을 Track으로 연결한다.
// Detection의 (x, y)는 bbox 좌상단, (width, height)는 크기로 가정한다.
class ByteTrackTracker : public Tracker
{
private:
    std::string config_path_;
    ByteTrackConfig config_;
    KalmanFilter kalman_;

    int frame_id_ = 0;
    int next_track_id_ = 0;
    int max_time_lost_ = 30;

    std::vector<bytetrack::STrackPtr> tracked_stracks_;
    std::vector<bytetrack::STrackPtr> lost_stracks_;

    void activate(bytetrack::STrack& track);
    void apply_detection(bytetrack::STrack& track, const bytetrack::STrack& det);

public:
    // 실행용: config_path의 YAML을 읽어 설정을 적용한다.
    explicit ByteTrackTracker(
        std::string config_path
    );

    // 테스트용: YAML 없이 설정 값을 직접 지정한다. (config_path는 로그 표시용)
    ByteTrackTracker(
        std::string config_path,
        const ByteTrackConfig& config
    );

    ~ByteTrackTracker() override = default;

    // Detection 목록을 이용해 ByteTrack Tracking을 수행한다.
    std::vector<Track> track(
        const std::vector<Detection>& detections
    ) override;

    // ByteTrack 내부 상태를 초기화한다.
    void reset() override;

    const ByteTrackConfig& config() const { return config_; }
};
