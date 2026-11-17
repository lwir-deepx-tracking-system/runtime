#pragma once

#include <chrono>
#include <deque>
#include <optional>
#include <vector>

#include "common/detection.hpp"
#include "common/track.hpp"

// Optional near/far policy. The caller owns model switching and reports its result.
class AutoModelSelector {
public:
    struct Config {
        int near_id = 0;
        int far_id = 0;
        double far_px = 100.0;
        double near_px = 140.0;
        double hold_s = 1.0;
        double dwell_s = 5.0;
        double lost_s = 0.5;
        double lost_occluded_s = 3.0;
        double near_backoff_s = 30.0;
        double near_backoff_max_s = 300.0;
        double median_s = 1.0;
        double min_score = 0.25;
        double max_px = 1440.0;
        double fast_hps = 1.0;
        double defer_s = 3.0;
        int max_per_minute = 4;
    };

    explicit AutoModelSelector(Config config);
    std::optional<int> observe(const DetectionResult& detections,
                               const TrackingResult& tracks, int active_model_id,
                               int followed_id = 0);
    void request_failed();

private:
    using TimePoint = std::chrono::steady_clock::time_point;
    Config config_;
    int current_ = 0;
    int pending_ = 0;
    TimePoint last_switch_{};
    TimePoint above_since_{};
    TimePoint below_since_{};
    TimePoint missing_since_{};
    bool last_occluded_ = false;
    bool near_seen_ = true;
    double near_backoff_s_ = 0.0;
    TimePoint near_block_until_{};
    std::deque<TimePoint> switches_;
    std::deque<std::pair<TimePoint, double>> heights_;
    TimePoint retry_after_{};
    TimePoint due_since_{};
    int due_target_ = 0;
    struct HeightSample {
        TimePoint at;
        double height;
        int track_id;
        double cx;
        double cy;
    };
    std::deque<HeightSample> signal_samples_;
    void changed(int active_model_id, TimePoint now);
    std::optional<int> want(int target, TimePoint now, bool force, bool fast = false);
};
