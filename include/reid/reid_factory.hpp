#pragma once

#include <memory>
#include "config/app_config.hpp"
#include "reid/i_reid_pipeline.hpp"

class ReidFactory
{
public:
    virtual ~ReidFactory() = default;
    static std::unique_ptr<ReidFactory> for_model(const ReidModelConfig& config);
    virtual std::unique_ptr<IReidPipeline> create(
        const ReidModelConfig& config, const CameraInputConfig& camera,
        const InferenceConfig& inference) const = 0;
};
