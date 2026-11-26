#pragma once

#include "config/app_config.hpp"
#include "detection/i_detection_pipeline.hpp"

#include <array>
#include <mutex>

// Schedules whole frames across two resident, independently bound detector sessions.
class FrameFeedPipeline final : public IDetectionPipeline {
public:
    FrameFeedPipeline(std::unique_ptr<IDetectionPipeline> first,
                      std::unique_ptr<IDetectionPipeline> second, NpuFeedMode mode);

    void submit_async(const FrameContextPtr& frame, Completion completion) override;

    void wait_idle() noexcept override;

    NpuFeedState npu_feed_state() const override;

    bool set_npu_feed(std::uint8_t mode) override;

private:
    using Clock = DetectionTiming::Clock;

    static double milliseconds(Clock::duration d);

    static void update_average(double& average, double sample);

    // Hysteresis keeps brief latency spikes from switching the feed.
    void observe(const DetectionTiming& timing);

    std::array<std::unique_ptr<IDetectionPipeline>, 2> sessions_;
    NpuFeedMode mode_;
    std::array<std::uint32_t, 2> submitted_{};
    bool alternating_;
    unsigned next_lane_ = 0;
    unsigned streak_ = 0;
    mutable std::mutex mutex_;
    Clock::time_point previous_capture_{};
    std::uint64_t previous_id_ = 0;
    double period_ms_ = 0;
    double service_ms_ = 0;
};
