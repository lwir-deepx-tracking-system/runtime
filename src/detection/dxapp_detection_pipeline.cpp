#include "detection/dxapp_detection_pipeline.hpp"

#include <stdexcept>

DxAppDetectionPipeline::DxAppDetectionPipeline(const AppConfig& config)
    : preprocessor_(config.model), model_path_(config.model.path),
      max_inflight_(config.detection.max_inflight) {}

DetectionResult DxAppDetectionPipeline::run(const Frame& frame)
{
    cv::Mat model_input;
    LwirPreprocessContext context;
    preprocessor_.process(frame.image, model_input, context);
    (void)model_input;
    (void)context;
    (void)model_path_;
    (void)max_inflight_;
    throw std::runtime_error("DXNN loading and NPU inference are not available in the CPU-only build");
}
