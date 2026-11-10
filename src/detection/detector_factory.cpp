#include "detection/detector_factory.hpp"

#include <stdexcept>
#include <utility>

#include "detection/dxrt_detection_pipeline.hpp"
#include "detection/lwir_preprocessor.hpp"
#include "detection/yolo26_postprocessor.hpp"
#include "detection/yolov8_postprocessor.hpp"

namespace
{
class YoloV8DetectorFactory final : public DetectorFactory
{
protected:
    std::unique_ptr<IPostprocessor> create_postprocessor(
        const ModelConfig& model_config) const override
    {
        return std::make_unique<YoloV8Postprocessor>(model_config);
    }
};

class Yolo26DetectorFactory final : public DetectorFactory
{
protected:
    std::unique_ptr<IPostprocessor> create_postprocessor(
        const ModelConfig& model_config) const override
    {
        return std::make_unique<Yolo26Postprocessor>(model_config);
    }
};
}  // namespace

std::unique_ptr<DetectorFactory> DetectorFactory::for_model(
    const ModelConfig& model_config)
{
    if (model_config.preprocess.type != "lwir_letterbox")
        throw std::invalid_argument(
            "Unsupported preprocess.type: " + model_config.preprocess.type);

    if (model_config.postprocess.type == "yolov8")
        return std::make_unique<YoloV8DetectorFactory>();
    if (model_config.postprocess.type == "yolo26")
        return std::make_unique<Yolo26DetectorFactory>();

    throw std::invalid_argument(
        "Unsupported postprocess.type: " + model_config.postprocess.type);
}

std::unique_ptr<IPreprocessor> DetectorFactory::create_preprocessor(
    const ModelConfig& model_config) const
{
    return std::make_unique<LwirPreprocessor>(model_config);
}

std::unique_ptr<IDetectionPipeline> DetectorFactory::create(
    const ModelConfig& model_config,
    const DetectionConfig& detection_config) const
{
    auto preprocessor = create_preprocessor(model_config);
    auto postprocessor = create_postprocessor(model_config);
    return std::make_unique<DxrtDetectionPipeline>(
        model_config, detection_config,
        std::move(preprocessor), std::move(postprocessor));
}
