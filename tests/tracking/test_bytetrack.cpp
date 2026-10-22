#include "tracking/bytetrack_tracker.hpp"

#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "detection_csv.hpp"

// 사용법: test_bytetrack [data 폴더]
// 인자가 없으면 tests/tracking/data/ 폴더를 사용한다.

namespace
{

Detection make_detection(float x, float y, float w, float h, float confidence)
{
    Detection d;
    d.x = x;
    d.y = y;
    d.width = w;
    d.height = h;
    d.confidence = confidence;
    d.class_id = 0;
    return d;
}

// YAML 문자열을 TrackingConfig로 읽는다. 잘못된 설정이면 std::runtime_error가 발생한다.
TrackingConfig parse_tracking(const std::string& yaml)
{
    return config_parser::load_tracking_config(YAML::Load(yaml));
}

// 잘못된 설정을 load_tracking_config()가 거부하는지 확인한다.
bool rejects(const std::string& name, const std::string& yaml)
{
    bool thrown = false;
    try
    {
        parse_tracking(yaml);
    }
    catch (const std::runtime_error& e)
    {
        thrown = true;
        std::cout << "  거부됨 (" << name << "): " << e.what() << "\n";
    }
    if (!thrown)
        std::cerr << "FAIL: 잘못된 설정(" << name << ")을 받아들임\n";
    return thrown;
}

// tracking 설정 읽기 검사: 선택 키를 생략하면 기능이 꺼진 기본값이어야 하고(기존 runtime·benchmark yaml과 호환),
// 지정하면 그대로 읽어야 하며, 범위를 벗어나거나 타입이 틀린 값은 거부해야 한다. 반환: 실패 수
int check_tracking_config()
{
    int failures = 0;
    const std::string base = "track_threshold: 0.5\nmatch_threshold: 0.8\ntrack_buffer: 30\n";

    try
    {
        const TrackingConfig off = parse_tracking(base);
        const TrackingConfig defaults;
        if (off.track_threshold != 0.5F || off.match_threshold != 0.8F || off.track_buffer != 30 ||
            off.fuse_score != defaults.fuse_score || off.lost_output_frames != 0 || off.buffer_scale != 0.0F ||
            off.buffer_max_px != 0.0F || off.buffer_small_size != 0.0F || off.buffer_snap ||
            off.low_threshold != 0.1F || off.new_track_threshold != 0.0F || off.low_match_threshold != 0.5F ||
            off.unconfirmed_match_threshold != 0.7F || off.nsa_kalman || off.kalman_format != "xyah")
        {
            std::cerr << "FAIL: 선택 키를 생략했는데 기능이 꺼진 기본값이 아님\n";
            ++failures;
        }

        const TrackingConfig on = parse_tracking(
            base + "fuse_score: true\nlost_output_frames: 1\nbuffer_scale: 0.5\n"
                   "buffer_max_px: 20\nbuffer_small_size: 40\nbuffer_snap: true\nnsa_kalman: true\n"
                   "kalman_format: xywh\n");
        if (!on.fuse_score || !on.nsa_kalman || on.kalman_format != "xywh" || on.lost_output_frames != 1 || on.buffer_scale != 0.5F || on.buffer_max_px != 20.0F ||
            on.buffer_small_size != 40.0F || !on.buffer_snap)
        {
            std::cerr << "FAIL: 선택 키를 지정했는데 값이 그대로 읽히지 않음\n";
            ++failures;
        }
        const TrackingConfig th = parse_tracking(
            base + "low_threshold: 0.05\nnew_track_threshold: 0.7\nlow_match_threshold: 0.6\n"
                   "unconfirmed_match_threshold: 0.8\n");
        if (th.low_threshold != 0.05F || th.new_track_threshold != 0.7F || th.low_match_threshold != 0.6F ||
            th.unconfirmed_match_threshold != 0.8F)
        {
            std::cerr << "FAIL: 세부 임계값을 지정했는데 값이 그대로 읽히지 않음\n";
            ++failures;
        }
        std::cout << "tracking 설정 읽기 성공 (선택 키 생략 / 지정)\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: 올바른 tracking 설정을 읽지 못함: " << e.what() << "\n";
        ++failures;
    }

    failures += !rejects("missing_key", "track_threshold: 0.5\nmatch_threshold: 0.8\n");
    failures += !rejects("out_of_range", "track_threshold: 1.5\nmatch_threshold: 0.8\ntrack_buffer: 30\n");
    failures += !rejects("zero_buffer", "track_threshold: 0.5\nmatch_threshold: 0.8\ntrack_buffer: 0\n");
    failures += !rejects("not_a_number", "track_threshold: high\nmatch_threshold: 0.8\ntrack_buffer: 30\n");
    failures += !rejects("not_a_bool", base + "fuse_score: maybe\n");
    failures += !rejects("negative_lost_output", base + "lost_output_frames: -1\n");
    failures += !rejects("negative_buffer_scale", base + "buffer_scale: -0.1\n");
    failures += !rejects("huge_buffer_scale", base + "buffer_scale: 3\n");
    failures += !rejects("negative_buffer_max_px", base + "buffer_max_px: -5\n");
    failures += !rejects("negative_buffer_small_size", base + "buffer_small_size: -5\n");
    failures += !rejects("low_above_track", base + "low_threshold: 0.5\n");
    failures += !rejects("negative_low", base + "low_threshold: -0.1\n");
    failures += !rejects("new_below_track", base + "new_track_threshold: 0.4\n");
    failures += !rejects("new_above_one", base + "new_track_threshold: 1.2\n");
    failures += !rejects("zero_low_match", base + "low_match_threshold: 0\n");
    failures += !rejects("huge_unconfirmed_match", base + "unconfirmed_match_threshold: 1.5\n");
    failures += !rejects("nsa_not_bool", base + "nsa_kalman: maybe\n");
    failures += !rejects("bad_kalman_format", base + "kalman_format: xyz\n");
    return failures;
}

// fuse_score 검사: 프레임 2의 검출은 이전 박스와 IoU 0.25, 점수 0.5다.
// 끄면 비용 0.75로 match_threshold(0.8) 안이라 같은 ID로 이어지고,
// 켜면 비용이 1 - 0.25 × 0.5 = 0.875가 되어 매칭되지 않아야 한다. 반환: 실패 수
int check_fuse_score()
{
    auto frame2 = [](bool fuse) {
        TrackingConfig c;
        c.fuse_score = fuse;
        ByteTrackTracker tracker(c);
        tracker.track({make_detection(100.0f, 100.0f, 40.0f, 40.0f, 0.9f)});
        return tracker.track({make_detection(124.0f, 100.0f, 40.0f, 40.0f, 0.5f)});
    };
    int failures = 0;
    const auto off = frame2(false), on = frame2(true);
    std::cout << "fuse_score 검사: 끔 출력 " << off.size() << "개, 켬 출력 " << on.size() << "개\n";
    if (off.size() != 1 || off.front().track_id != 1)
    {
        std::cerr << "FAIL: fuse_score를 끄면 IoU 0.25 검출이 같은 ID로 이어져야 함\n";
        ++failures;
    }
    if (!on.empty())
    {
        std::cerr << "FAIL: fuse_score를 켰는데 점수가 낮은 검출이 매칭됨\n";
        ++failures;
    }
    return failures;
}

// lost_output_frames 검사: 30x30 물체가 매 프레임 3px씩 오른쪽으로 움직이다가 프레임 11부터 검출이 사라진다.
//   끔(0)            : 프레임 11에 출력 없음
//   1                : 프레임 11에 같은 ID의 예측 박스 1개, 프레임 12에는 출력 없음
//   화면 오른쪽 끝(검출 박스가 경계에 닿음)에서 사라지는 경우:
//   영상 크기를 모르면 예측 박스를 내고, set_image_size()를 지정하면 나가는 중이므로 내지 않아야 한다.
// 반환: 실패 수
int check_lost_output()
{
    auto run = [](int lost_frames, bool with_image_size, float start_x) {
        TrackingConfig c;
        c.lost_output_frames = lost_frames;
        ByteTrackTracker tracker(c);
        if (with_image_size)
            tracker.set_image_size(640.0f, 480.0f);
        std::vector<std::vector<Track>> out;  // out[f - 1] = 프레임 f의 출력
        for (int f = 1; f <= 12; ++f)
        {
            std::vector<Detection> dets;
            if (f <= 10)
                dets.push_back(make_detection(start_x + 3.0f * (f - 1), 200.0f, 30.0f, 30.0f, 0.9f));
            out.push_back(tracker.track(dets));
        }
        return out;
    };
    int failures = 0;

    const auto none = run(0, false, 100.0f);
    const auto one = run(1, false, 100.0f);
    std::cout << "lost_output 검사: 프레임 11 출력 끔 " << none[10].size() << "개, L1 " << one[10].size()
              << "개 / 프레임 12 L1 " << one[11].size() << "개\n";
    if (!none[10].empty())
    {
        std::cerr << "FAIL: lost_output_frames=0인데 놓친 Track이 출력됨\n";
        ++failures;
    }
    if (one[9].size() != 1 || one[10].size() != 1 || one[10].front().track_id != one[9].front().track_id)
    {
        std::cerr << "FAIL: lost_output_frames=1이면 첫 누락 프레임에 같은 ID의 예측 박스가 나와야 함\n";
        ++failures;
    }
    if (!one[11].empty())
    {
        std::cerr << "FAIL: lost_output_frames=1인데 두 번째 누락 프레임에도 출력됨\n";
        ++failures;
    }

    // 마지막 검출 박스가 x = 610..640(오른쪽 경계)에 닿아 있다. 예측 박스는 아직 대부분 화면 안이다.
    const auto edge_unknown = run(1, false, 583.0f);
    const auto edge_known = run(1, true, 583.0f);
    std::cout << "  화면 밖 억제: 프레임 11 출력 영상 크기 모름 " << edge_unknown[10].size() << "개, 지정 "
              << edge_known[10].size() << "개\n";
    if (edge_unknown[10].size() != 1)
    {
        std::cerr << "FAIL: 영상 크기를 모르면 예측 박스를 내야 함 (검사 조건이 유효한지 확인)\n";
        ++failures;
    }
    if (!edge_known[10].empty())
    {
        std::cerr << "FAIL: 화면 밖으로 나가는 Track의 예측 박스가 출력됨\n";
        ++failures;
    }
    return failures;
}

// CMC 검사: 정지한 물체(20x40)를 카메라가 좌우로 30px씩 흔들며 찍는다 (짐벌 진동).
// 화면 속 물체가 매 프레임 30px씩 반대로 튀어 직전 위치와 IoU가 0이고 속도 예측으로도 따라갈 수 없다.
// 보정하면 12프레임 모두 같은 ID로 출력되어야 하고, 보정 없이는 그러지 못해야 한다. 반환: 실패 수
int check_camera_motion()
{
    constexpr int kFrames = 12;
    // 반환: 첫 프레임 ID가 출력된 프레임 수, 사용된 ID 수
    auto run = [](bool cmc) {
        ByteTrackTracker tracker(TrackingConfig{});
        auto cam = [](int f) { return (f % 2) * 30.0f; };  // 카메라 위치 0, 30, 0, 30, ...
        int first_id = -1, kept = 0;
        std::set<int> ids;
        for (int f = 0; f < kFrames; ++f)
        {
            const Detection d = make_detection(500.0f - cam(f), 200.0f, 20.0f, 40.0f, 0.9f);
            if (cmc && f > 0)
                tracker.set_camera_motion(CameraMotion::translation(-(cam(f) - cam(f - 1)), 0.0));
            for (const auto& t : tracker.track({d}))
            {
                if (first_id < 0)
                    first_id = t.track_id;
                ids.insert(t.track_id);
                kept += (t.track_id == first_id);
            }
        }
        return std::make_pair(kept, ids.size());
    };
    int failures = 0;
    const auto without = run(false), with = run(true);
    std::cout << "CMC 검사 (" << kFrames << "프레임): 보정 없이 같은 ID " << without.first << "프레임, 보정하면 "
              << with.first << "프레임 (ID " << with.second << "개)\n";
    if (with.first != kFrames || with.second != 1)
    {
        std::cerr << "FAIL: 카메라 움직임을 보정했는데 모든 프레임에서 같은 ID로 출력되지 않음\n";
        ++failures;
    }
    if (without.first == kFrames)
    {
        std::cerr << "FAIL: 검사 조건이 너무 쉬움 (보정 없이도 모든 프레임 유지)\n";
        ++failures;
    }

    // 항등 변환은 결과를 바꾸지 않아야 한다
    KalmanFilter kf;
    KalmanState s = kf.initiate({100.0, 50.0, 0.5, 40.0});
    const KalmanState before = s;
    apply_camera_motion(s, CameraMotion{});
    if (s.mean != before.mean || s.covariance != before.covariance)
    {
        std::cerr << "FAIL: 항등 카메라 움직임이 Kalman 상태를 바꿈\n";
        ++failures;
    }
    // 평행이동은 중심만 옮기고 속도·공분산은 그대로여야 한다
    apply_camera_motion(s, CameraMotion::translation(-7.0, 3.0));
    if (std::abs(s.mean[0] - 93.0) > 1e-9 || std::abs(s.mean[1] - 53.0) > 1e-9 ||
        s.mean[4] != before.mean[4] || s.covariance != before.covariance)
    {
        std::cerr << "FAIL: 평행이동 보정 결과가 다름\n";
        ++failures;
    }
    return failures;
}


// Buffered IoU 검사: 30x30 물체가 (3, 1)px/프레임으로 움직이다가 10프레임이 통째로 빠진다.
// Tracker는 빠진 프레임을 모르므로 예측은 1스텝만 움직이고, 실제 박스는 (33, 11)px 떨어져 IoU가 0이 된다.
//   끔                          : 다시 나타난 물체가 새 ID여야 한다 (검사 조건이 유효한지 확인)
//   buffer_scale 0.5            : 같은 ID를 유지해야 한다
//   buffer_scale 0.5 + 20px 이하 : 30px 물체는 대상이 아니므로 끔과 같아야 한다
// 반환: 실패 수
int check_buffered_iou()
{
    auto run = [](float scale, float small_size) {
        TrackingConfig c;
        c.buffer_scale = scale;
        c.buffer_small_size = small_size;
        ByteTrackTracker tracker(c);
        std::set<int> ids;
        // 1~20프레임 호출, 21~30 누락, 31~40프레임 호출
        for (int f = 1; f <= 40; ++f)
        {
            if (f > 20 && f <= 30)
                continue;  // 프레임 누락: track()을 호출하지 않는다
            const Detection d = make_detection(100.0f + 3.0f * f, 200.0f + 1.0f * f, 30.0f, 30.0f, 0.9f);
            for (const auto& t : tracker.track({d}))
                ids.insert(t.track_id);
        }
        return ids.size();
    };
    int failures = 0;
    const size_t off = run(0.0f, 0.0f), on = run(0.5f, 0.0f), filtered = run(0.5f, 20.0f);
    std::cout << "Buffered IoU 검사 (10프레임 누락, 30px 물체): 사용한 ID 수 끔 " << off << "개, buffer 0.5 " << on
              << "개, buffer 0.5 + 20px 이하만 " << filtered << "개\n";
    if (off < 2)
    {
        std::cerr << "FAIL: 검사 조건이 너무 쉬움 (Buffered IoU 없이도 같은 ID 유지)\n";
        ++failures;
    }
    if (on != 1)
    {
        std::cerr << "FAIL: Buffered IoU를 켰는데 같은 ID를 유지하지 못함\n";
        ++failures;
    }
    if (filtered != off)
    {
        std::cerr << "FAIL: buffer_small_size보다 큰 물체에 Buffered IoU가 적용됨\n";
        ++failures;
    }

    // buffer_snap: 누락 직후 프레임(31)의 출력 x가 검출 x(193)와 같아야 하고, 끄면 예측 쪽으로 끌려가야 한다
    auto x_after_gap = [](bool snap) {
        TrackingConfig c;
        c.buffer_scale = 0.5f;
        c.buffer_snap = snap;
        ByteTrackTracker tracker(c);
        float x = -1.0f;
        for (int f = 1; f <= 31; ++f)
        {
            if (f > 20 && f <= 30)
                continue;
            const Detection d = make_detection(100.0f + 3.0f * f, 200.0f + 1.0f * f, 30.0f, 30.0f, 0.9f);
            const auto tracks = tracker.track({d});
            if (f == 31 && !tracks.empty())
                x = tracks.front().x;
        }
        return x;
    };
    const float x_snap = x_after_gap(true), x_blend = x_after_gap(false);
    std::cout << "  누락 직후 출력 x: 검출 193, buffer_snap 켬 " << x_snap << ", 끔 " << x_blend << "\n";
    if (std::abs(x_snap - 193.0f) > 0.5f || x_blend >= 192.0f)
    {
        std::cerr << "FAIL: buffer_snap 동작이 다름\n";
        ++failures;
    }
    return failures;
}

// NSA Kalman: 정지한 물체(x=100)를 10프레임 추적한 뒤 검출이 x=110으로 튀었을 때의 출력 x.
// 켜면 점수 높은 검출(0.95)은 끈 경우보다 검출 쪽으로 더 붙고, 점수 낮은 검출(0.3, 2차 매칭)은
// 점수 높은 검출보다 덜 붙어야 한다. 끄면 점수와 상관없이 같아야 한다. 반환: 실패 수
int check_nsa_kalman()
{
    auto x_after_jump = [](bool nsa, float jump_score) {
        TrackingConfig c;
        c.nsa_kalman = nsa;
        ByteTrackTracker tracker(c);
        float x = -1.0f;
        for (int f = 1; f <= 11; ++f)
        {
            const float score = (f == 11) ? jump_score : 0.95f;
            const Detection d = make_detection(f == 11 ? 110.0f : 100.0f, 200.0f, 40.0f, 80.0f, score);
            const auto tracks = tracker.track({d});
            if (f == 11 && !tracks.empty())
                x = tracks.front().x;
        }
        return x;
    };
    const float off_high = x_after_jump(false, 0.95f), off_low = x_after_jump(false, 0.3f);
    const float on_high = x_after_jump(true, 0.95f), on_low = x_after_jump(true, 0.3f);
    std::cout << "NSA Kalman 검사 (검출 x 100 -> 110): 끔 " << off_high << " / " << off_low
              << ", 켬 점수 0.95 " << on_high << ", 점수 0.3 " << on_low << "\n";
    int failures = 0;
    if (off_high < 100.0f || std::abs(off_high - off_low) > 1e-3f)
    {
        std::cerr << "FAIL: nsa_kalman을 껐는데 검출 점수에 따라 위치 보정이 달라짐\n";
        ++failures;
    }
    if (!(on_high > off_high && on_high <= 110.0f))
    {
        std::cerr << "FAIL: nsa_kalman을 켰는데 점수 높은 검출 쪽으로 더 붙지 않음\n";
        ++failures;
    }
    if (!(on_low < on_high && on_low > 100.0f))
    {
        std::cerr << "FAIL: nsa_kalman을 켰는데 점수 낮은 검출이 점수 높은 검출보다 덜 반영되지 않음\n";
        ++failures;
    }
    return failures;
}

// XYWH Kalman: 높이는 그대로(80)이고 폭만 40 → 80으로 10프레임 동안 늘어나는 물체.
// 마지막 프레임의 출력 폭이 XYWH에서 XYAH보다 검출 폭(80)에 더 가까워야 한다.
// 정지·크기 고정 물체는 두 형식의 출력이 같은 위치·크기여야 한다. 반환: 실패 수
int check_kalman_format()
{
    auto run = [](const std::string& fmt, bool grow) {
        TrackingConfig c;
        c.kalman_format = fmt;
        ByteTrackTracker tracker(c);
        Track last{};
        for (int f = 1; f <= 20; ++f)
        {
            const float w = (grow && f > 10) ? 40.0f + 4.0f * (f - 10) : 40.0f;
            const auto tracks = tracker.track({make_detection(200.0f - w / 2, 200.0f, w, 80.0f, 0.9f)});
            if (!tracks.empty())
                last = tracks.front();
        }
        return last;
    };
    const Track ah = run("xyah", true), wh = run("xywh", true);
    const Track ah_s = run("xyah", false), wh_s = run("xywh", false);
    std::cout << "Kalman 형식 검사 (폭 40 -> 80, 높이 80 고정): 마지막 출력 폭 xyah " << ah.width << ", xywh " << wh.width
              << " / 크기 고정 물체 폭 xyah " << ah_s.width << ", xywh " << wh_s.width << "\n";
    int failures = 0;
    if (!(std::abs(wh.width - 80.0f) < std::abs(ah.width - 80.0f)))
    {
        std::cerr << "FAIL: xywh가 폭 변화를 xyah보다 빨리 따라가지 못함\n";
        ++failures;
    }
    if (std::abs(ah_s.width - 40.0f) > 0.5f || std::abs(wh_s.width - 40.0f) > 0.5f ||
        std::abs(ah_s.x - wh_s.x) > 0.5f || std::abs(ah_s.height - wh_s.height) > 0.5f)
    {
        std::cerr << "FAIL: 크기가 고정된 물체에서 xyah와 xywh 출력이 다름\n";
        ++failures;
    }
    return failures;
}

}  // namespace


int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : test_csv::tracking_test_dir() + "/data";

    test_csv::Sequence seq;
    const std::string error = test_csv::load_sequence(dir, seq);
    if (!error.empty())
    {
        std::cerr << error << "\n";
        return 1;
    }

    // runtime 기본값과 같은 typed 설정으로 ByteTrack을 생성한다.
    ByteTrackTracker tracker(TrackingConfig{});
    std::map<int, std::set<int>> ids_by_frame;
    int failures = 0;

    for (int f : seq.frames)
    {
        const auto& dets = seq.by_frame[f];  // 없으면 빈 목록
        const auto tracks = tracker.track(dets);

        std::cout << "frame " << f << ": detections " << dets.size()
                  << " -> tracks " << tracks.size();
        for (const auto& t : tracks)
        {
            std::cout << "  id=" << t.track_id << " (" << t.x << ", " << t.y
                      << ", " << t.width << ", " << t.height
                      << ", score " << t.confidence << ")";
            ids_by_frame[f].insert(t.track_id);
        }
        std::cout << "\n";

        // 검사 1: 검출이 없는 프레임에서는 출력 Track이 없어야 한다.
        if (dets.empty() && !tracks.empty())
        {
            std::cerr << "FAIL: frame " << f << " 검출이 없는데 Track이 출력됨\n";
            ++failures;
        }
    }

    // 검사 2: 첫 프레임의 ID 중 하나는 검출이 있는 이후 모든 프레임에서 유지되어야 한다.
    // (낮은 점수 프레임은 2차 매칭으로, 검출이 사라졌다 돌아온 프레임은 Lost 복구로 유지)
    const auto& frames = seq.frames;
    bool kept = false;
    for (int id : ids_by_frame[frames.front()])
    {
        bool all = true;
        for (size_t i = 1; i < frames.size(); ++i)
            if (!seq.by_frame[frames[i]].empty() && !ids_by_frame[frames[i]].count(id))
                all = false;
        if (all)
        {
            kept = true;
            std::cout << "track_id " << id << "가 모든 검출 프레임에서 유지됨\n";
        }
    }
    if (!kept)
    {
        std::cerr << "FAIL: 첫 프레임의 ID가 이후 프레임에서 유지되지 않음\n";
        ++failures;
    }

    // 검사 3: reset() 후에는 ID가 1부터 다시 시작해야 한다.
    tracker.reset();
    const auto again = tracker.track(seq.by_frame[frames.front()]);
    if (!again.empty() && again.front().track_id != 1)
    {
        std::cerr << "FAIL: reset() 후 ID가 1부터 시작하지 않음\n";
        ++failures;
    }

    // 검사 4: 추가 기능(설정 읽기, fuse_score, lost_output, CMC, Buffered IoU)
    failures += check_tracking_config();
    failures += check_fuse_score();
    failures += check_lost_output();
    failures += check_camera_motion();
    failures += check_buffered_iou();
    failures += check_nsa_kalman();
    failures += check_kalman_format();

    if (failures > 0)
    {
        std::cerr << "ByteTrack 테스트 실패: " << failures << "건\n";
        return 1;
    }
    std::cout << "ByteTrack 테스트 통과\n";
    return 0;
}
