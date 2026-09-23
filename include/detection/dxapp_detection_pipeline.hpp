

#pragma once

#include "app/app_config.hpp"
#include "common/detection.hpp"
#include "common/frame.hpp"
#include "detection/lwir_preprocessor.hpp"

class DetectionPipeline {
public:
    virtual ~DetectionPipeline() = default;
    virtual DetectionResult run(const Frame& frame) = 0;
};

// 향후 dx_app 실행기와 연결할 CPU 측 경계이다.
// 현재는 하드웨어 추론을 제공하지 않으며 가짜 검출 결과도 생성하지 않는다.
class DxAppDetectionPipeline final : public DetectionPipeline {
public:
    explicit DxAppDetectionPipeline(const AppConfig& config);
    DetectionResult run(const Frame& frame) override;
private:
    LwirPreprocessor preprocessor_;
    std::string model_path_;
    std::size_t max_inflight_;
};
