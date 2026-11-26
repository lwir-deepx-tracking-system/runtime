#include "detection/frame_feed_pipeline.hpp"

#include <chrono>
#include <memory>
#include <vector>

namespace {
class Session final : public IDetectionPipeline {
public:
    Session(unsigned lane, int latency, std::vector<unsigned>& calls)
        : lane_(lane), latency_(latency), calls_(calls) {}

    void submit_async(const FrameContextPtr& frame, Completion done) override
    {
        calls_.push_back(lane_);
        DetectionTiming timing;
        timing.frame_id = frame->metadata.frame_id;
        timing.preprocess_started_at = frame->metadata.captured_at;
        timing.postprocess_finished_at = timing.preprocess_started_at +
            std::chrono::milliseconds(latency_);
        done({}, {}, timing);
    }

    void wait_idle() noexcept override {}

private:
    unsigned lane_;
    int latency_;
    std::vector<unsigned>& calls_;
};

bool check(NpuFeedMode mode, int latency, std::vector<unsigned> expected)
{
    std::vector<unsigned> calls;
    unsigned completed = 0;
    FrameFeedPipeline feed(std::make_unique<Session>(0, latency, calls),
                           std::make_unique<Session>(1, latency, calls), mode);
    for (unsigned i = 1; i <= expected.size(); ++i) {
        auto frame = std::make_shared<FrameContext>();
        frame->metadata.frame_id = i;
        frame->metadata.captured_at = DetectionTiming::TimePoint{} + std::chrono::milliseconds(i * 33);
        feed.submit_async(frame, [&](auto, auto error, auto timing) {
            if (!error && timing.frame_id == i) ++completed;
        });
    }
    feed.wait_idle();
    return calls == expected && completed == expected.size();
}
}

int main()
{
    std::vector<unsigned> expected(20, 0);
    expected[17] = expected[19] = 1;
    return check(NpuFeedMode::Alternate, 12, {0, 1, 0, 1}) &&
           check(NpuFeedMode::Auto, 12, std::vector<unsigned>(20, 0)) &&
           check(NpuFeedMode::Auto, 50, expected) ? 0 : 1;
}
