#pragma once

#include <memory>
#include <mutex>

#include "detection/i_detection_pipeline.hpp"

class ActivePipelineSlot
{
private:
    mutable std::mutex mutex_;
    std::shared_ptr<IDetectionPipeline> pipeline_;

public:
    std::shared_ptr<IDetectionPipeline> snapshot() const;
    std::shared_ptr<IDetectionPipeline> take();
    void set(std::shared_ptr<IDetectionPipeline> pipeline);
};
