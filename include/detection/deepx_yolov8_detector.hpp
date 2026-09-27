#pragma once

#include <string>

#include "detection/detector.hpp"

// DX-AllSuite dx_app의 detection runner 구조를 프로젝트 경계에 맞게 감싼다.
// 실제 구현에서는 이 객체가 DX 전처리기, InferenceEngine, 후처리기를 함께 소유한다.
class DeepxYoloV8Detector : public Detector
{
private:
    std::string config_path_;
    std::string model_path_;
    int input_width_ = 0;
    int input_height_ = 0;
    float confidence_threshold_ = 0.0f;
    float nms_threshold_ = 0.0f;

    void load_config();

public:
    explicit DeepxYoloV8Detector(std::string config_path);

    std::vector<Detection> detect(const FrameContext& frame) override;
};
