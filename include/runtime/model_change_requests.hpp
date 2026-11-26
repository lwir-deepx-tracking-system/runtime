#pragma once
#include "app/pipeline_requests.hpp"
#include "pipeline/model_manager_thread.hpp"
#include <functional>

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
        ThreadSafeQueue<ModelChangeRequest>& requests, const std::function<int()>& active_id);

    // Called after any required measurement flush.
    void complete(const ModelChangeResult& result);

    bool take_suspend_request();

    void cancel_pending();
};
