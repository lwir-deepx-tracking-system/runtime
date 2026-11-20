#pragma once
#include "app/pipeline_requests.hpp"
#include "pipeline/model_manager_thread.hpp"
#include <functional>
#include <limits>

// Owns pending model request tickets. ModelManagerThread owns engine transitions.
class ModelChangeRequests {
    std::mutex model_change_mutex_;
    std::uint64_t next_model_request_id_ = 1;
    std::map<std::uint64_t, std::shared_ptr<std::promise<PipelineResult>>> model_completions_;
    std::atomic<bool> suspend_for_model_{false};
public:
    PipelineChangeTicket request(std::uint32_t model_id,
        const std::map<int, ModelCatalogEntry>& catalog, const std::atomic<bool>& ready,
        RuntimeControl& control, ThreadSafeQueue<FrameMessage>& frames,
        ThreadSafeQueue<ModelChangeRequest>& requests, const std::function<int()>& active_id)
    {
        if (model_id == 0 || model_id > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || !catalog.count(static_cast<int>(model_id))) return {PipelineResult::InvalidId, {}};
        std::lock_guard<std::mutex> lock(model_change_mutex_);
        if (!ready.load() || control.stop_requested.load())
            return {PipelineResult::NotReady, {}};
        const auto state = control.state.load();
        if (!model_completions_.empty() || (state != RuntimeState::Running && state != RuntimeState::SwitchFailed))
            return {PipelineResult::Busy, {}};
        auto completion = std::make_shared<std::promise<PipelineResult>>();
        auto future = completion->get_future().share();
        if (state == RuntimeState::Running && active_id() == static_cast<int>(model_id)) {
            completion->set_value(PipelineResult::Ok);
            return {PipelineResult::Ok, std::move(future)};
        }
        const auto request_id = next_model_request_id_++;
        model_completions_.emplace(request_id, completion);
        control.set_detection_quiesced(false);
        control.state.store(RuntimeState::Switching);
        frames.interrupt_waiters();
        suspend_for_model_.store(true);
        if (!requests.push({request_id, catalog.at(static_cast<int>(model_id))})) {
            model_completions_.erase(request_id);
            control.state.store(state);
            return {PipelineResult::NotReady, {}};
        }
        return {PipelineResult::Ok, std::move(future)};
    }

    // Called after any required measurement flush.
    void complete(const ModelChangeResult& result)
    {
        {
            std::lock_guard<std::mutex> lock(model_change_mutex_);
            const auto pending = model_completions_.find(result.request_id);
            if (pending != model_completions_.end()) {
                pending->second->set_value(result.outcome == ModelChangeOutcome::Activated ? PipelineResult::Ok :
                    result.outcome == ModelChangeOutcome::Restored ? PipelineResult::Restored : PipelineResult::RestoreFailed);
                model_completions_.erase(pending);
            }
        }
    }

    bool take_suspend_request() { return suspend_for_model_.exchange(false); }

    void cancel_pending()
    {
        std::lock_guard<std::mutex> lock(model_change_mutex_);
        for (auto& entry : model_completions_) entry.second->set_value(PipelineResult::NotReady);
        model_completions_.clear();
    }
};
