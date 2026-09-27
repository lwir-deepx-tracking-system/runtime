#include "detection/dxapp_detection_pipeline.hpp"

#include <stdexcept>

// Detection에 필요한 전처리 설정, 모델 경로와 비동기 처리 한도를 한 객체가 소유한다.
DxAppDetectionPipeline::DxAppDetectionPipeline(const AppConfig& config)
    : preprocessor_(config.model), model_path_(config.model.path),
      max_inflight_(config.detection.max_inflight) {}

// 전처리부터 후처리까지 dx_app runner 한 단계에서 수행하는 연결 지점이다.
std::vector<Detection> DxAppDetectionPipeline::detect(const FrameContext& frame)
{
    cv::Mat model_input;
    LwirPreprocessContext context;
    preprocessor_.process(frame.image, model_input, context);

    // TODO: dx_app 비동기 runner에 model_input을 제출하고 context로 원본 좌표를 복원한다.
    (void)model_input;
    (void)context;
    (void)model_path_;
    (void)max_inflight_;

    // NPU가 없는 빌드에서는 실제 결과처럼 보이는 가짜 detection을 만들지 않는다.
    throw std::runtime_error("DXNN loading and NPU inference are not available in the CPU-only build");
}
