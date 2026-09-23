#include "pipeline/detection_thread.hpp"

class FakePipeline final : public DetectionPipeline {
public:
    DetectionResult run(const Frame&) override
    {
        DetectionResult result;
        result.detections.push_back(Detection{1, 2, 3, 4, 0.9F, 0, "target"});
        return result;
    }
};

int main()
{
    ThreadSafeQueue<Frame> input;
    ThreadSafeQueue<DetectionResult> output;
    FakePipeline pipeline;
    DetectionThread thread(pipeline, input, output, true);
    thread.start();
    Frame frame;
    frame.metadata.frame_id = 42;
    frame.metadata.captured_at = std::chrono::steady_clock::now();
    frame.metadata.enqueued_at = frame.metadata.captured_at;
    input.push(std::move(frame));
    input.close();
    DetectionResult result;
    if (!output.pop(result)) return 1;
    thread.join();
    if (result.metadata.frame_id != 42 || result.detections.size() != 1) return 2;
    if (thread.metrics().size() != 1 || thread.metrics()[0].frame_id != 42) return 3;
    DetectionResult extra;
    if (output.pop(extra)) return 4;
    return 0;
}
