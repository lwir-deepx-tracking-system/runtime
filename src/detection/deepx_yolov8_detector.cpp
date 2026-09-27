#include "detection/deepx_yolov8_detector.hpp"

#include <utility>

#include <yaml-cpp/yaml.h>

#include "common/logger.hpp"

DeepxYoloV8Detector::DeepxYoloV8Detector(std::string config_path)
    : config_path_(std::move(config_path))
{
    load_config();
    Logger::info("[Detection] DeepxYoloV8Detector 생성 완료");
}

void DeepxYoloV8Detector::load_config()
{
    const YAML::Node config = YAML::LoadFile(config_path_);

    model_path_ = config["model"]["path"].as<std::string>();
    input_width_ = config["input"]["width"].as<int>();
    input_height_ = config["input"]["height"].as<int>();
    confidence_threshold_ =
        config["postprocess"]["confidence_threshold"].as<float>();
    nms_threshold_ = config["postprocess"]["nms_threshold"].as<float>();

    Logger::debug("[Detection] 모델: " + model_path_);
    Logger::debug(
        "[Detection] 입력 크기: " + std::to_string(input_width_) + "x" +
        std::to_string(input_height_));
}

std::vector<Detection> DeepxYoloV8Detector::detect(const FrameContext& frame)
{
    (void)frame;
    (void)confidence_threshold_;
    (void)nms_threshold_;

    // TODO:
    // 1. DX DetectionPreprocessor로 frame.image 전처리
    // 2. DX-RT InferenceEngine으로 NPU 추론
    // 3. DX YOLOv8Postprocessor로 결과 변환 및 NMS
    // 4. 프로젝트 Detection 목록으로 변환
    return {};
}
