#include <chrono>
#include <memory>
#include <thread>

#include "pipeline/detection_thread.hpp"
#include "pipeline/model_manager_thread.hpp"

namespace {
// 실제 추론 없이 모델 전환 대기 동작만 검증하는 파이프라인이다.
class IdlePipeline final : public IDetectionPipeline {
public:
    // 비동기 미완료 작업이 없는 테스트 구현의 명시적 합류 계약.
    void wait_idle() noexcept override {}

    void submit_async(const FrameContextPtr&, Completion) override {}
};
}

// 입력 프레임이 없어도 알림만으로 모델 교체가 진행되는지 확인한다.
int main()
{
    RuntimeControl control;
    ActivePipelineSlot slot;
    ThreadSafeQueue<FrameMessage> frames;
    ThreadSafeQueue<DetectionResult> detections;
    ThreadSafeQueue<TrackingResultPtr> gui;
    ThreadSafeQueue<ModelChangeRequest> requests;
    ModelCatalogEntry initial; initial.id = 1;
    ModelCatalogEntry next; next.id = 2;
    auto create = [](const ModelCatalogEntry&) -> std::shared_ptr<IDetectionPipeline> {
        return std::make_shared<IdlePipeline>();
    };
    ModelManagerThread manager(initial, {}, requests, slot, control, nullptr, create);
    DetectionThread detector(control, slot, frames, detections, gui, false, false);
    manager.start();
    manager.wait_until_initialized();
    detector.start();

    // No camera frame arrives. The control interrupt must still quiesce detection.
    control.set_detection_quiesced(false);
    control.state.store(RuntimeState::Switching);
    frames.interrupt_waiters();
    requests.push({1, next});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (control.generation.load() != 1 &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool switched = control.state.load() == RuntimeState::Running &&
        control.generation.load() == 1 && manager.active_model_id() == 2;

    control.request_stop();
    frames.close();
    requests.close();
    detector.join();
    manager.join();
    return switched ? 0 : 1;
}
