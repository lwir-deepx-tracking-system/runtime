#pragma once

#include <memory>
#include <detection/i_preprocessor.hpp>
#include <detection/i_postprocessor.hpp>
#include "config/app_config.hpp"
#include "detection/i_detection_pipeline.hpp"

class DxrtDetectionPipeline final : public IDetectionPipeline
{
public:
    DxrtDetectionPipeline(
        const ModelConfig& model_config,
        const DetectionConfig& detection_config,
        std::unique_ptr<IPreprocessor> preprocessor,
        std::unique_ptr<IPostprocessor> postprocessor);
    ~DxrtDetectionPipeline() override;

    void submit_async(
        const FrameContextPtr& frame,
        Completion completion) override;
    void wait_idle() override;

private:
    struct NpuState;

    std::unique_ptr<IPreprocessor> preprocessor_;
    std::unique_ptr<IPostprocessor> postprocessor_;
    std::unique_ptr<NpuState> npu_;
};
