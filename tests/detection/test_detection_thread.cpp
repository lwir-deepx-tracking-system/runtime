#include "pipeline/detection_thread.hpp"

#include <utility>

// NPU 없이 DetectionThread의 queue 및 Frame 수명 계약만 검증하는 test double.
class FakePipeline final : public IDetectionPipeline {
private:
    struct Pending
    {
        FrameContextPtr frame;
        Completion completion;
    };

    std::vector<Pending> pending_;

public:
    void submit_async(
        const FrameContextPtr& frame,
        Completion completion) override
    {
        pending_.push_back(Pending{frame, std::move(completion)});
    }

    // 실제 비동기 callback처럼 제출 순서와 반대로 완료한다.
    void wait_idle() override
    {
        for (auto pending = pending_.rbegin(); pending != pending_.rend(); ++pending)
        {
            Detection detection;
            detection.width = 3.0F;
            detection.height = 4.0F;
            detection.confidence = 0.9F;
            detection.class_id = 0;
            detection.class_name = "target";

            DetectionTiming timing;
            timing.frame_id = pending->frame->metadata.frame_id;
            const auto now = DetectionTiming::Clock::now();
            timing.preprocess_started_at = now;
            timing.preprocess_finished_at = now;
            timing.inference_started_at = now;
            timing.inference_finished_at = now;
            timing.postprocess_started_at = now;
            timing.postprocess_finished_at = now;
            timing.npu_processing_duration_ms =
                static_cast<double>(timing.frame_id);
            pending->completion({detection}, nullptr, timing);
        }
        pending_.clear();
    }
};

// Detection 결과가 Camera의 동일 FrameContext를 유지하고 queue를 닫는지 확인한다.
int main()
{
    RuntimeControl runtime_control;
    runtime_control.state.store(RuntimeState::Running);
    ActivePipelineSlot active_pipeline;
    active_pipeline.set(std::make_shared<FakePipeline>());

    ThreadSafeQueue<FrameMessage> input;
    ThreadSafeQueue<DetectionResult> output;
    ThreadSafeQueue<TrackingResultPtr> gui_output;
    DetectionThread thread(
        runtime_control,
        active_pipeline,
        input,
        output,
        gui_output,
        false,
        true);
    thread.start();

    // 서로 다른 두 요청을 제출해 callback 완료 순서가 뒤집혀도 귀속이
    // 유지되는지 확인한다.
    auto first_frame = std::make_shared<FrameContext>();
    first_frame->metadata.frame_id = 42;
    first_frame->metadata.captured_at = std::chrono::steady_clock::now();
    FrameMessage first_message;
    first_message.frame = first_frame;
    first_message.enqueued_at = first_frame->metadata.captured_at;
    input.push(std::move(first_message));

    auto second_frame = std::make_shared<FrameContext>();
    second_frame->metadata.frame_id = 43;
    second_frame->metadata.captured_at = std::chrono::steady_clock::now();
    FrameMessage second_message;
    second_message.frame = second_frame;
    second_message.enqueued_at = second_frame->metadata.captured_at;
    input.push(std::move(second_message));
    input.close();

    DetectionResult first_result;
    DetectionResult second_result;
    if (!output.pop(first_result) || !output.pop(second_result)) return 1;
    thread.join();

    // 결과는 제출 순서로 나오며 각 timing은 callback을 만든 프레임에 귀속된다.
    if (first_result.frame != first_frame ||
        first_result.frame->metadata.frame_id != 42 ||
        first_result.detections.size() != 1 ||
        second_result.frame != second_frame ||
        second_result.frame->metadata.frame_id != 43 ||
        second_result.detections.size() != 1)
        return 2;
    if (thread.metrics().size() != 2 ||
        thread.detection_timings().size() != 2)
        return 3;
    if (thread.detection_timings()[0].frame_id != 43 ||
        thread.detection_timings()[0].npu_processing_ms() != 43.0 ||
        thread.detection_timings()[1].frame_id != 42 ||
        thread.detection_timings()[1].npu_processing_ms() != 42.0)
        return 3;

    // 입력 종료가 downstream queue 종료로 전파되어 추가 pop은 실패해야 한다.
    DetectionResult extra;
    if (output.pop(extra)) return 4;
    return 0;
}
