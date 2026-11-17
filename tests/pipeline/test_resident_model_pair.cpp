#include "pipeline/model_manager_thread.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {
class Pipeline final : public IDetectionPipeline {
public:
    void wait_idle() noexcept override {}
    void submit_async(const FrameContextPtr&, Completion) override {}
};
bool wait(RuntimeControl& control, int generation) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < end) {
        if (control.generation.load() == static_cast<std::uint64_t>(generation)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
}

int main() {
    ThreadSafeQueue<ModelChangeRequest> requests;
    ActivePipelineSlot slot;
    RuntimeControl control;
    ModelCatalogEntry near; near.id = 12;
    ModelCatalogEntry far; far.id = 13;
    std::atomic<int> created{0};
    auto make = [&](const ModelCatalogEntry&) -> std::shared_ptr<IDetectionPipeline> {
        created.fetch_add(1);
        return std::make_shared<Pipeline>();
    };
    ModelManagerThread manager(near, {}, requests, slot, control, nullptr, make, nullptr, far);
    manager.start();
    manager.wait_until_initialized();
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (created.load() < 2 && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    int result = created.load() == 2 ? 0 : 1;
    control.set_detection_quiesced(true);
    control.state.store(RuntimeState::Switching);
    requests.push({1, far});
    if (!wait(control, 1) || manager.active_model_id() != 13 || created.load() != 2)
        result = 2;
    control.state.store(RuntimeState::Switching);
    requests.push({2, near});
    if (!wait(control, 2) || manager.active_model_id() != 12 || created.load() != 2)
        result = 3;
    control.request_stop();
    requests.close();
    manager.join();
    return result;
}
