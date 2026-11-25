#include "detection/detector_factory.hpp"

#include <stdexcept>
#include <fstream>
#include <openssl/evp.h>
#include "tiled_detection_pipeline.hpp"
#include "frame_feed_pipeline.hpp"
#include <utility>

#include "detection/dxrt_detection_pipeline.hpp"
#include "detection/lwir_preprocessor.hpp"
#include "detection/yolo26_postprocessor.hpp"
#include "detection/yolov8_postprocessor.hpp"

namespace
{
// Verify pinned artifacts before loading an inference engine.
void verify_model_asset(const std::string& path, const std::string& expected)
{
    std::ifstream input(path, std::ios::binary);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!input || !digest || EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Cannot verify detector asset: " + path);
    char buffer[65536];
    while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
        if (EVP_DigestUpdate(digest.get(), buffer, static_cast<std::size_t>(input.gcount())) != 1)
            throw std::runtime_error("Cannot hash detector asset: " + path);
    }
    unsigned char raw[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (input.bad() || EVP_DigestFinal_ex(digest.get(), raw, &length) != 1)
        throw std::runtime_error("Cannot hash detector asset: " + path);
    std::string hex;
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < length; ++i) {
        hex += digits[raw[i] >> 4];
        hex += digits[raw[i] & 15];
    }
    if (hex != expected) throw std::runtime_error("Detector SHA-256 mismatch: " + path);
}

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
    const InferenceConfig& inference_config) const
{
    if (inference_config.npu_feed != NpuFeedMode::Single) {
        if (inference_config.npu_cores != NpuCoreBinding::Core01 ||
            !model_config.tile_model_path.empty())
            throw std::invalid_argument("Frame feed requires a full-frame model and npu_cores: 01");
        auto binding = inference_config;
        binding.npu_feed = NpuFeedMode::Single;
        binding.npu_cores = NpuCoreBinding::Core0;
        auto first = create(model_config, binding);
        binding.npu_cores = NpuCoreBinding::Core1;
        auto second = create(model_config, binding);
        return std::make_unique<FrameFeedPipeline>(
            std::move(first), std::move(second), inference_config.npu_feed);
    }
    if (!model_config.tile_model_path.empty()) {
        if (inference_config.npu_cores != NpuCoreBinding::Core01)
            throw std::invalid_argument("Tiled detection requires npu_cores: 01");
        verify_model_asset(model_config.path, model_config.sha256);
        verify_model_asset(model_config.tile_model_path, model_config.tile_sha256);
        auto tile = model_config;
        tile.path = model_config.tile_model_path;
        tile.tile_model_path.clear();
        tile.camera_input.width = tile.input.width = 384;
        tile.postprocess.nms_mode = PostprocessNmsMode::Disabled;
        auto binding = inference_config;
        binding.npu_cores = NpuCoreBinding::Core0;
        auto left = std::make_unique<DxrtDetectionPipeline>(tile, binding,
            std::make_unique<TilePreprocessor>(tile), create_postprocessor(tile));
        binding.npu_cores = NpuCoreBinding::Core1;
        auto right = std::make_unique<DxrtDetectionPipeline>(tile, binding,
            std::make_unique<TilePreprocessor>(tile), create_postprocessor(tile));
        return std::make_unique<TiledDetectionPipeline>(std::move(left), std::move(right));
    }
    if (!model_config.sha256.empty())
        verify_model_asset(model_config.path, model_config.sha256);
    auto preprocessor = create_preprocessor(model_config);
    auto postprocessor = create_postprocessor(model_config);
    return std::make_unique<DxrtDetectionPipeline>(
        model_config, inference_config,
        std::move(preprocessor), std::move(postprocessor));
}
