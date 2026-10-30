#include "pipeline/active_pipeline_slot.hpp"

#include <utility>

std::shared_ptr<IDetectionPipeline> ActivePipelineSlot::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pipeline_;
}

std::shared_ptr<IDetectionPipeline> ActivePipelineSlot::take()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(pipeline_);
}

void ActivePipelineSlot::set(std::shared_ptr<IDetectionPipeline> pipeline)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_ = std::move(pipeline);
}
