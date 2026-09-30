#pragma once

#include <array>
#include <memory>
#include <vector>

#include "app/app_config.hpp"
#include "tracking/kalman_filter.hpp"
#include "tracking/tracker.hpp"

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
    static constexpr float kLowThreshold = 0.1F;

    TrackingConfig config_;
    float new_track_threshold_ = 0.6F;
    KalmanFilter kalman_;

    int frame_id_ = 0;
    int next_track_id_ = 0;
    int max_time_lost_ = 30;

    std::vector<bytetrack::STrackPtr> tracked_stracks_;
    std::vector<bytetrack::STrackPtr> lost_stracks_;

    void activate(bytetrack::STrack& track);
    void apply_detection(bytetrack::STrack& track, const bytetrack::STrack& det);

public:
    // AppConfig가 검증한 runtime tracking 설정을 적용한다.
    explicit ByteTrackTracker(const TrackingConfig& config);

    ~ByteTrackTracker() override = default;

    // Detection 목록을 이용해 ByteTrack Tracking을 수행한다.
    std::vector<Track> track(
        const std::vector<Detection>& detections
    ) override;

    // ByteTrack 내부 상태를 초기화한다.
    void reset() override;

    const TrackingConfig& config() const { return config_; }
};
