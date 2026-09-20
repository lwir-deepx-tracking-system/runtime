#include "preprocess/yolov8_preprocessor.hpp"

#include <utility>

#include <yaml-cpp/yaml.h>

#include "common/logger.hpp"


YoloV8Preprocessor::YoloV8Preprocessor(
    std::string config_path)
    : config_path_(std::move(config_path))
{
    load_config();

    Logger::info(
        "[Preprocess] YoloV8Preprocessor 생성 완료"
    );
}


// ---------------------------------------------------------
// config/model/yolov8n.yaml을 생성 시 한 번 읽는다.
// ---------------------------------------------------------
void YoloV8Preprocessor::load_config()
{
    YAML::Node config = YAML::LoadFile(config_path_);

    input_width_ = config["input"]["width"].as<int>();

    input_height_ = config["input"]["height"].as<int>();

    resize_method_ = config["preprocess"]["resize"].as<std::string>();

    normalize_ = config["preprocess"]["normalize"].as<bool>();

    // Model YAML에서 읽은 전처리 설정 확인
    Logger::debug(
        "[Preprocess] 입력 크기: " +
        std::to_string(input_width_) + "x" +
        std::to_string(input_height_)
    );

    Logger::debug(
        "[Preprocess] 크기 조정 방식: " +
        resize_method_
    );

    Logger::debug(
        "[Preprocess] 정규화 적용: " +
        std::string(normalize_ ? "true" : "false")
    );
}

// ---------------------------------------------------------
// YOLOv8 실제 전처리
// ---------------------------------------------------------
ModelInput YoloV8Preprocessor::preprocess(const Frame& frame)
{
    ModelInput input;

    if (resize_method_ == "letterbox")
    {
        // letterbox
    }
    else if (resize_method_ == "resize")
    {
        // direct resize
    }

    if (normalize_)
    {
        // normalization
    }

    // tensor 변환

    return input;
}
