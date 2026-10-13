#pragma once

#include <memory>
#include <vector>

#include "config/app_config.hpp"
#include "detection/i_detection_pipeline.hpp"
#include "detection/lwir_preprocessor.hpp"
#include "detection/postprocessor.hpp"

class DxAppDetectionPipeline final : public IDetectionPipeline
{
public:
    DxAppDetectionPipeline(
        const ModelConfig& model_config,
        const DetectionConfig& detection_config);
    ~DxAppDetectionPipeline() override;

    std::vector<Detection> detect(const FrameContext& frame) override;
    void submit_async(
        const std::shared_ptr<FrameContext>& frame,
        Completion completion) override;
    void wait_idle() override;

private:
    struct NpuState;

    std::unique_ptr<IPreprocessor> preprocessor_;
    std::unique_ptr<IPostprocessor> postprocessor_;
    std::unique_ptr<NpuState> npu_;
};
