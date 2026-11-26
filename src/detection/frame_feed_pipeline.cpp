#include "detection/frame_feed_pipeline.hpp"

#include "common/logger.hpp"
#include <utility>

FrameFeedPipeline::FrameFeedPipeline(std::unique_ptr<IDetectionPipeline> first,
                  std::unique_ptr<IDetectionPipeline> second, NpuFeedMode mode)
    : sessions_{std::move(first), std::move(second)}, mode_(mode), alternating_(mode == NpuFeedMode::Alternate) {}

void FrameFeedPipeline::submit_async(const FrameContextPtr& frame, Completion completion)
{
    unsigned lane;
    {
        std::lock_guard lock(mutex_);
        const auto captured = frame->metadata.captured_at;
        if (previous_capture_ != Clock::time_point{} && captured > previous_capture_ &&
            frame->metadata.frame_id > previous_id_) {
            const auto period = milliseconds(captured - previous_capture_) /
                (frame->metadata.frame_id - previous_id_);
            if (period >= 5 && period <= 250) update_average(period_ms_, period);
        }
        previous_capture_ = captured;
        previous_id_ = frame->metadata.frame_id;
        lane = alternating_ ? next_lane_ : 0;
        if (alternating_) next_lane_ ^= 1;
        ++submitted_[lane];
    }
    sessions_[lane]->submit_async(frame,
        [this, lane, completion = std::move(completion)](
            std::vector<Detection> boxes, std::exception_ptr error, DetectionTiming timing) {
            if (lane == 0 && !error) observe(timing);
            completion(std::move(boxes), error, std::move(timing));
        });
}

void FrameFeedPipeline::wait_idle() noexcept
{
    for (auto& session : sessions_) session->wait_idle();
}

NpuFeedState FrameFeedPipeline::npu_feed_state() const
{
    std::lock_guard lock(mutex_);
    return {true, static_cast<std::uint8_t>(mode_), alternating_, submitted_[0], submitted_[1]};
}

bool FrameFeedPipeline::set_npu_feed(std::uint8_t mode)
{
    if (mode > static_cast<std::uint8_t>(NpuFeedMode::Auto)) return false;
    std::lock_guard lock(mutex_);
    const auto next = static_cast<NpuFeedMode>(mode);
    if (mode_ == next) return true;
    mode_ = next;
    alternating_ = mode_ == NpuFeedMode::Alternate;
    streak_ = next_lane_ = 0;
    service_ms_ = 0;
    return true;
}

double FrameFeedPipeline::milliseconds(Clock::duration d)
{
    return std::chrono::duration<double, std::milli>(d).count();
}

void FrameFeedPipeline::update_average(double& average, double sample)
{
    average = average > 0 ? average * 0.9 + sample * 0.1 : sample;
}

void FrameFeedPipeline::observe(const DetectionTiming& timing)
{
    if (timing.preprocess_started_at == Clock::time_point{} ||
        timing.postprocess_finished_at <= timing.preprocess_started_at) return;
    std::lock_guard lock(mutex_);
    if (mode_ != NpuFeedMode::Auto) return;
    update_average(service_ms_, milliseconds(
        timing.postprocess_finished_at - timing.preprocess_started_at));
    if (period_ms_ <= 0) return;
    const bool change = alternating_ ? service_ms_ < period_ms_ * 0.75 :
                                      service_ms_ > period_ms_ * 1.05;
    streak_ = change ? streak_ + 1 : 0;
    if (streak_ >= (alternating_ ? 90u : 15u)) {
        alternating_ = !alternating_;
        next_lane_ = 0;
        streak_ = 0;
        Logger::info(alternating_ ? "[Detection] NPU feed: alternating 0/1" :
                                    "[Detection] NPU feed: single core 0");
    }
}
