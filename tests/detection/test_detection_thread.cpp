#include "pipeline/detection_thread.hpp"

// NPU 없이 DetectionThread의 queue 및 Frame 수명 계약만 검증하는 test double.
class FakePipeline final : public IDetectionPipeline {
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

// Detection 결과가 Camera의 동일 FrameContext를 유지하고 queue를 닫는지 확인한다.
int main()
{
    ThreadSafeQueue<FrameMessage> input;
    ThreadSafeQueue<DetectionResult> output;
    FakePipeline pipeline;
    DetectionThread thread(pipeline, input, output, true);
    thread.start();

    // 실제 pipeline과 동일하게 shared FrameMessage 한 개를 입력한다.
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

    // 복사된 Frame이 아니라 같은 shared_ptr이며 측정 ID도 유지되어야 한다.
    if (result.frame != frame || result.frame->metadata.frame_id != 42 ||
        result.detections.size() != 1) return 2;
    if (thread.metrics().size() != 1 || thread.metrics()[0].frame_id != 42) return 3;

    // 입력 종료가 downstream queue 종료로 전파되어 추가 pop은 실패해야 한다.
    DetectionResult extra;
    if (output.pop(extra)) return 4;
    return 0;
}
