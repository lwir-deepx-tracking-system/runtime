#include "detection/tiled_detection_pipeline.hpp"

#include <stdexcept>

// Exercises failure and lifetime without producing synthetic inference results.
class DeferredFailure final : public IDetectionPipeline {
    Completion pending_;
    std::uint64_t frame_ = 0;
    bool fail_on_submit_;

public:
    explicit DeferredFailure(bool fail_on_submit) : fail_on_submit_(fail_on_submit) {}

    void submit_async(const FrameContextPtr& frame, Completion done) override
    {
        if (frame->image.type() != CV_8UC1 || frame->image.size() != cv::Size(384, 480))
            throw std::logic_error("Invalid tile handed to child");
        if (fail_on_submit_) throw std::runtime_error("submit failed");
        frame_ = frame->metadata.frame_id;
        pending_ = std::move(done);
    }

    void wait_idle() noexcept override
    {
        if (!pending_) return;
        DetectionTiming timing;
        timing.frame_id = frame_;
        auto done = std::move(pending_);
        done({}, std::make_exception_ptr(std::runtime_error("job failed")), std::move(timing));
    }
};

int main()
{
    auto frame = std::make_shared<FrameContext>();
    frame->image = cv::Mat(480, 640, CV_16UC1, cv::Scalar(1234));
    frame->metadata.frame_id = 42;
    for (bool left_fails : {false, true}) {
        TiledDetectionPipeline pipeline(std::make_unique<DeferredFailure>(left_fails),
            std::make_unique<DeferredFailure>(!left_fails));
        int completed = 0;
        bool valid = false;
        pipeline.submit_async(frame, [&](auto boxes, auto error, auto timing) {
            ++completed;
            valid = boxes.empty() && error && timing.frame_id == 42;
        });
        if (completed != 0) return 1;
        pipeline.wait_idle();
        pipeline.wait_idle();
        if (completed != 1 || !valid) return 2;
    }
}
