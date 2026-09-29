#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "app/app_config.hpp"
#include "common/detection.hpp"
#include "common/frame.hpp"
#include "detection/lwir_preprocessor.hpp"
#include "detection/yolov8_postprocessor.hpp"

// dx_app과 같은 방식으로 전처리, 추론, 후처리를 한 객체 안에서 수행한다.
class DetectionPipeline
{
public:
    virtual ~DetectionPipeline() = default;
    virtual std::vector<Detection> detect(const FrameContext& frame) = 0;
};

// 향후 dx_app 실행기와 연결할 CPU 측 경계이다.
// 현재는 하드웨어 추론을 제공하지 않으며 가짜 검출 결과도 생성하지 않는다.
class DxAppDetectionPipeline final : public DetectionPipeline
{
public:
    explicit DxAppDetectionPipeline(const AppConfig& config);
    std::vector<Detection> detect(const FrameContext& frame) override;

private:
    LwirPreprocessor preprocessor_;
    Yolov8Postprocessor postprocessor_;
    std::string model_path_;
    std::size_t max_inflight_;
};
