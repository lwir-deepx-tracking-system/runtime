#include "reid/reid_factory.hpp"
#include "reid/reid_pipeline.hpp"
#include "common/dxrt_inference_option.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <dxrt/dxrt_cxx_api.h>

namespace {
class RepVggReidModel final : public IReidModel
{
public:
    RepVggReidModel(const ReidModelConfig& config, const InferenceConfig& inference)
        : config_(config), engine_(config.path, make_dxrt_inference_option(inference))
    {
        const auto inputs = engine_.GetInputs();
        const auto outputs = engine_.GetOutputs();
        const std::vector<int64_t> input_shape =
            {1, config.input_height, config.input_width, config.input_channels};
        const std::vector<int64_t> output_shape = {1, config.output_dimensions};
        if (inputs.size() != 1 || inputs[0].shape() != input_shape ||
            inputs[0].type() != dxrt::DataType::UINT8 ||
            engine_.GetInputSize() !=
                static_cast<std::uint64_t>(config.input_height) *
                static_cast<std::uint64_t>(config.input_width) *
                static_cast<std::uint64_t>(config.input_channels))
            throw std::runtime_error("ReID DXNN input contract does not match YAML");
        if (outputs.size() != 1 || outputs[0].name() != config.output_name ||
            outputs[0].shape() != output_shape || outputs[0].type() != dxrt::DataType::FLOAT)
            throw std::runtime_error("ReID DXNN output contract does not match YAML");
    }

    std::vector<float> infer(const cv::Mat& input) override
    {
        if (input.type() != CV_8UC3 || input.cols != config_.input_width ||
            input.rows != config_.input_height || !input.isContinuous())
            throw std::invalid_argument("ReID input does not match the DXNN tensor contract");
        const auto outputs = engine_.Run(input.data);
        if (outputs.size() != 1 || !outputs[0] ||
            outputs[0]->name() != config_.output_name ||
            outputs[0]->shape() != std::vector<int64_t>{1, config_.output_dimensions} ||
            outputs[0]->type() != dxrt::DataType::FLOAT || !outputs[0]->data() ||
            outputs[0]->size_in_bytes() < static_cast<std::size_t>(config_.output_dimensions) * sizeof(float))
            throw std::runtime_error("ReID inference returned an invalid embedding");
        const auto* data = static_cast<const float*>(outputs[0]->data());
        return std::vector<float>(data, data + config_.output_dimensions);
    }

private:
    ReidModelConfig config_;
    dxrt::InferenceEngine engine_;
};

class RepVggReidFactory final : public ReidFactory
{
public:
    std::unique_ptr<IReidPipeline> create(
        const ReidModelConfig& config, const CameraInputConfig& camera,
        const InferenceConfig& inference) const override
    {
        return std::make_unique<ReidPipeline>(
            std::make_unique<LwirReidPreprocessor>(config, camera),
            std::make_unique<RepVggReidModel>(config, inference),
            std::make_unique<L2ReidPostprocessor>(config.output_dimensions));
    }
};
}  // namespace

std::unique_ptr<ReidFactory> ReidFactory::for_model(const ReidModelConfig& config)
{
    if (config.name == "repvgg") return std::make_unique<RepVggReidFactory>();
    throw std::invalid_argument("Unsupported ReID model: " + config.name);
}
