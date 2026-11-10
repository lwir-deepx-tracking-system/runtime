#include "pipeline/detection_thread.hpp"

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>

namespace {
// 실제 추론 없이 제출과 콜백 반환 사이의 정지 절차를 제어한다.
class DeferredPipeline final : public IDetectionPipeline {
    std::mutex mutex_;
    std::condition_variable condition_;
    Completion completion_;
    std::uint64_t frame_id_ = 0;
    bool submitted_ = false, finished_ = false, waiting_ = false;
public:
    void submit_async(const FrameContextPtr& frame, Completion completion) override {
        std::lock_guard<std::mutex> lock(mutex_);
        completion_ = std::move(completion);
        frame_id_ = frame->metadata.frame_id;
        submitted_ = true;
        condition_.notify_all();
    }
    // 콜백이 반환하기 전에는 어떤 대기자도 완료로 판단하지 못하게 한다.
    void wait_idle() noexcept override {
        std::unique_lock<std::mutex> lock(mutex_);
        waiting_ = true;
        condition_.notify_all();
        condition_.wait(lock, [&] { return finished_; });
    }
    // 테스트가 원하는 시점에 빈 결과 콜백을 호출하고 반환 완료를 게시한다.
    void finish(bool error = false) {
        Completion completion;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completion = std::move(completion_);
        }
        if (completion) {
            DetectionTiming timing;
            timing.frame_id = frame_id_;
            completion({}, error ? std::make_exception_ptr(std::runtime_error("expected inference failure")) :
                std::exception_ptr{}, timing);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        condition_.notify_all();
    }
    // 시간 제한을 두어 잘못된 구현에서도 테스트를 정리할 수 있다.
    bool await(bool waiting) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(1), [&] {
            return waiting ? waiting_ : submitted_;
        });
    }
};

// 슬롯 소유권을 잃은 종료와 모델 전환 정지 확인을 각각 검증한다.
bool check(bool switching) {
    RuntimeControl control;
    ActivePipelineSlot slot;
    auto pipeline = std::make_shared<DeferredPipeline>();
    slot.set(pipeline);
    ThreadSafeQueue<FrameMessage> frames;
    ThreadSafeQueue<DetectionResult> results;
    ThreadSafeQueue<TrackingResultPtr> gui;
    DetectionThread detector(control, slot, frames, results, gui, false, false);
    detector.start();
    auto frame = std::make_shared<FrameContext>();
    frame->metadata.frame_id = 42;
    FrameMessage message; message.frame = frame;
    frames.push(std::move(message));
    bool ok = pipeline->await(false);
    std::future<bool> acknowledged;
    if (switching) {
        control.set_detection_quiesced(false);
        control.state.store(RuntimeState::Switching);
        frames.interrupt_waiters();
        acknowledged = std::async(std::launch::async, [&] { return control.wait_detection_quiesced(); });
    } else {
        // 모델 매니저가 슬롯을 먼저 take한 상황에서도 탐지 스레드는 완료를 기다려야 한다.
        slot.take();
        control.request_stop();
        frames.close();
    }
    ok = pipeline->await(true) && ok;
    if (switching)
        ok = acknowledged.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout && ok;
    pipeline->finish();
    if (switching) {
        const bool ready = acknowledged.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
        ok = ready && ok;
        if (ready) {
            ok = acknowledged.get() && ok;
            // 정지 확인 시 탐지 스레드가 엔진 소멸을 방해하는 참조를 남기면 안 된다.
            slot.take();
            ok = pipeline.use_count() == 1 && ok;
        }
        control.request_stop();
        frames.close();
    }
    detector.join();
    DetectionResult result;
    ok = !detector.failed() && results.pop(result) && result.frame == frame && ok;
    DetectionResult extra;
    return !results.pop(extra) && ok;
}
// 추론 오류도 콜백으로 보고한 뒤 합류하며, 비어 있는 입력 대기를 중단시킨다.
bool check_callback_error() {
    RuntimeControl control;
    ActivePipelineSlot slot;
    auto pipeline = std::make_shared<DeferredPipeline>();
    slot.set(pipeline);
    ThreadSafeQueue<FrameMessage> frames;
    ThreadSafeQueue<DetectionResult> results;
    ThreadSafeQueue<TrackingResultPtr> gui;
    DetectionThread detector(control, slot, frames, results, gui, false, false);
    detector.start();
    auto frame = std::make_shared<FrameContext>();
    frame->metadata.frame_id = 99;
    FrameMessage message; message.frame = frame;
    frames.push(std::move(message));
    const bool submitted = pipeline->await(false);
    pipeline->finish(true);
    // 제출 자체가 실패한 테스트에서도 작업자를 정리한다.
    if (!submitted) { control.request_stop(); frames.close(); }
    detector.join();
    DetectionResult result;
    return submitted && detector.failed() && control.stop_requested.load() &&
        detector.error_message() == "expected inference failure" &&
        !results.pop(result) && frames.push(FrameMessage{});
}

}

// SIGINT/SIGQUIT이 공유하는 파이프라인 종료 경로의 경쟁 조건을 CPU에서 검증한다.
int main() {
    if (!check(false)) return 1;
    if (!check(true)) return 2;
    if (!check_callback_error()) return 3;
    return 0;
}
