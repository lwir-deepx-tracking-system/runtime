#include "detection/detector_factory.hpp"

#include <stdexcept>

std::unique_ptr<DetectorFactory> DetectorFactory::for_model(
    const ModelConfig&)
{
    throw std::runtime_error(
        "DetectorFactory requires a build with LWIR_ENABLE_DX_APP=ON");
}

std::unique_ptr<IDetectionPipeline> DetectorFactory::create(
    const ModelConfig&, const DetectionConfig&) const
{
    throw std::runtime_error(
        "DetectorFactory requires a build with LWIR_ENABLE_DX_APP=ON");
}

std::unique_ptr<IPreprocessor> DetectorFactory::create_preprocessor(
    const ModelConfig&) const
{
    throw std::runtime_error(
        "DetectorFactory requires a build with LWIR_ENABLE_DX_APP=ON");
}
