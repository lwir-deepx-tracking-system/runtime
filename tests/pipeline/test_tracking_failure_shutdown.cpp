#include "pipeline/tracking_thread.hpp"

#include <stdexcept>

namespace {
// 표준 예외와 알 수 없는 예외에 대해 소비자 실패 처리 계약을 검증한다.
class FailingTracker final : public Tracker {
    bool unknown_;
public:
    explicit FailingTracker(bool unknown) : unknown_(unknown) {}
    std::vector<Track> track(const std::vector<Detection>&) override {
        if (unknown_) throw 1;
        throw std::runtime_error("expected tracking failure");
    }
    void reset() override {}
};

// 소비자가 실패해도 탐지 생산자는 남은 콜백을 게시한 후 직접 큐를 닫을 수 있다.
bool check(bool unknown) {
    RuntimeControl control;
    TrackingCatalogEntry entry; entry.id = 1;
    TrackerManager manager({{1, entry}}, 1,
        [unknown](const TrackingConfig&) { return std::make_shared<FailingTracker>(unknown); });
    ThreadSafeQueue<DetectionResult> detections;
    ThreadSafeQueue<TrackingResultPtr> controls, gui;
    TrackingThread tracking(manager, control, detections, controls, gui, false);
    tracking.start();
    DetectionResult first;
    first.frame = std::make_shared<FrameContext>();
    detections.push(std::move(first));
    tracking.join();
    if (!tracking.failed() || !control.stop_requested.load() ||
        control.state.load() != RuntimeState::Stopping) return false;
    DetectionResult late;
    late.frame = std::make_shared<FrameContext>();
    const auto frame = late.frame;
    if (!detections.push(std::move(late))) return false;
    detections.close(); // 이 테스트가 탐지 생산자의 역할로 종료를 확정한다.
    DetectionResult received;
    if (!detections.pop(received) || received.frame != frame || detections.pop(received)) return false;
    TrackingResultPtr extra;
    return !controls.pop(extra) && !gui.pop(extra);
}
}

int main() {
    if (!check(false)) return 1;
    if (!check(true)) return 2;
    return 0;
}
