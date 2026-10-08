#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "config/app_config.hpp"
#include "common/detection.hpp"
#include "common/frame.hpp"
#include "common/common_util.hpp"

#include "detection/i_detection_pipeline.hpp"
#include "detection/postprocessor.hpp"
#include "detection/lwir_preprocessor.hpp"


class DxAppDetectionPipeline final : public IDetectionPipeline
{
public:
    DxAppDetectionPipeline(
        const ModelConfig& model_config,
        const DetectionConfig& detection_config);
    ~DxAppDetectionPipeline() override;

    std::vector<Detection> detect(const FrameContext& frame) override;

private:
    struct NpuState;

    std::unique_ptr<IPreprocessor> preprocessor_;
    std::unique_ptr<IPostprocessor> postprocessor_;
    std::string model_path_;
    std::size_t max_inflight_;
    std::unique_ptr<NpuState> npu_;
};
