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

// Hold the slot through the short query so a GUI caller cannot retain a DXRT session.
NpuFeedState ActivePipelineSlot::npu_feed_state() const
{
    std::lock_guard lock(mutex_);
    return pipeline_ ? pipeline_->npu_feed_state() : NpuFeedState{};
}

bool ActivePipelineSlot::set_npu_feed(std::uint8_t mode)
{
    std::lock_guard lock(mutex_);
    return pipeline_ && pipeline_->set_npu_feed(mode);
}
