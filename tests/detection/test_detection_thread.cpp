#include "pipeline/detection_thread.hpp"

class FakePipeline final : public DetectionPipeline {
public:
    std::vector<Detection> detect(const FrameContext&) override
    {
        Detection detection;
        detection.width = 3.0F;
        detection.height = 4.0F;
        detection.confidence = 0.9F;
        detection.class_id = 0;
        detection.class_name = "target";
        return {detection};
    }
};

int main()
{
    ThreadSafeQueue<FrameMessage> input;
    ThreadSafeQueue<DetectionResult> output;
    FakePipeline pipeline;
    DetectionThread thread(pipeline, input, output, true);
    thread.start();
    auto frame = std::make_shared<FrameContext>();
    frame->metadata.frame_id = 42;
    frame->metadata.captured_at = std::chrono::steady_clock::now();
    FrameMessage message;
    message.frame = frame;
    message.enqueued_at = frame->metadata.captured_at;
    input.push(std::move(message));
    input.close();
    DetectionResult result;
    if (!output.pop(result)) return 1;
    thread.join();
    if (result.frame != frame || result.frame->metadata.frame_id != 42 ||
        result.detections.size() != 1) return 2;
    if (thread.metrics().size() != 1 || thread.metrics()[0].frame_id != 42) return 3;
    DetectionResult extra;
    if (output.pop(extra)) return 4;
    return 0;
}
