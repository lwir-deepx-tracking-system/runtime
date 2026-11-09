#pragma once

#include <memory>

#include "config/app_config.hpp"
#include "detection/i_detection_pipeline.hpp"
#include "detection/i_postprocessor.hpp"
#include "detection/i_preprocessor.hpp"

class DetectorFactory
{
public:
    virtual ~DetectorFactory() = default;

    static std::unique_ptr<DetectorFactory> for_model(
        const ModelConfig& model_config);

    std::unique_ptr<IDetectionPipeline> create(
        const ModelConfig& model_config,
        const DetectionConfig& detection_config) const;

protected:
    virtual std::unique_ptr<IPostprocessor> create_postprocessor(
        const ModelConfig& model_config) const = 0;

    virtual std::unique_ptr<IPreprocessor> create_preprocessor(
        const ModelConfig& model_config) const;
};
