#include <chrono>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "pipeline/model_manager_thread.hpp"

namespace {
class Pipeline final : public IDetectionPipeline {
public:
    void submit_async(const FrameContextPtr&, Completion) override {}
};

bool wait_for(RuntimeControl& control, RuntimeState state, std::uint64_t generation)
{
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (control.state.load() == state &&
            control.generation.load() == generation) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
bool wait_result(ThreadSafeQueue<ModelChangeResult>& queue, ModelChangeResult& result)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (queue.try_pop(result)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
}

int main()
{
    ThreadSafeQueue<ModelChangeRequest> requests;
    ThreadSafeQueue<ModelChangeResult> results;
    ActivePipelineSlot slot;
    RuntimeControl control;
    ModelCatalogEntry initial;
    initial.id = 1;
    ModelCatalogEntry broken;
    broken.id = 2;
    ModelCatalogEntry replacement;
    replacement.id = 3;
    std::vector<int> creations;
    std::atomic<bool> fail_restore{false};
    std::weak_ptr<IDetectionPipeline> first;
    auto create = [&](const ModelCatalogEntry& entry)
        -> std::shared_ptr<IDetectionPipeline> {
        creations.push_back(entry.id);
        if (entry.id == 2 || (entry.id == 3 && fail_restore.load()))
            throw std::runtime_error("model load failed");
        auto pipeline = std::make_shared<Pipeline>();
        if (creations.size() == 1) first = pipeline;
        return pipeline;
    };
    ModelManagerThread manager(initial, {}, requests, slot, control, &results, create);
    manager.start();
    manager.wait_until_initialized();
    int result = 0;
    if (manager.active_model_id() != 1 || !slot.snapshot()) result = 1;

    control.state.store(RuntimeState::Switching);
    control.set_detection_quiesced(true);
    requests.push({1, broken});
    if (!wait_for(control, RuntimeState::Running, 1)) result = 2;
    else if (manager.active_model_id() != 1 || !slot.snapshot() ||
        !first.expired() || creations != std::vector<int>({1, 2, 1})) result = 3;
    ModelChangeResult event;
    if (!wait_result(results, event) || event.request_id != 1 ||
        event.requested_model_id != 2 || event.active_model_id != 1 ||
        event.outcome != ModelChangeOutcome::Restored || event.error.empty()) result = 10;

    control.state.store(RuntimeState::Switching);
    requests.push({2, replacement});
    if (!wait_for(control, RuntimeState::Running, 2)) result = 4;
    else if (manager.active_model_id() != 3 || !slot.snapshot() ||
        creations != std::vector<int>({1, 2, 1, 3})) result = 5;
    if (!wait_result(results, event) || event.request_id != 2 ||
        event.outcome != ModelChangeOutcome::Activated || event.active_model_id != 3)
        result = 11;

    fail_restore.store(true);
    control.state.store(RuntimeState::Switching);
    requests.push({3, broken});
    if (!wait_for(control, RuntimeState::SwitchFailed, 2)) result = 6;
    else if (manager.active_model_id() != 0 || slot.snapshot() ||
             manager.error_message().find("rollback failed") == std::string::npos)
        result = 7;
    if (!wait_result(results, event) || event.request_id != 3 ||
        event.outcome != ModelChangeOutcome::Failed || event.active_model_id != 0)
        result = 12;

    fail_restore.store(false);
    control.state.store(RuntimeState::Switching);
    requests.push({4, replacement});
    if (!wait_for(control, RuntimeState::Running, 3)) result = 8;
    else if (manager.active_model_id() != 3 || !slot.snapshot()) result = 9;
    if (!wait_result(results, event) || event.request_id != 4 ||
        event.outcome != ModelChangeOutcome::Activated || event.active_model_id != 3)
        result = 13;
    control.request_stop();
    requests.close();
    manager.join();
    return result;
}
