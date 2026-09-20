#include "postprocess/yolov8_postprocessor.hpp"

#include <string>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "common/logger.hpp"


YoloV8Postprocessor::YoloV8Postprocessor(
    std::string config_path)
    : config_path_(std::move(config_path))
{
    load_config();

    Logger::info(
        "[Postprocess] YoloV8Postprocessor 생성 완료"
    );
}


// Model YAML에서 confidence threshold와 NMS threshold를 읽는다.
void YoloV8Postprocessor::load_config()
{
    const YAML::Node config =
        YAML::LoadFile(config_path_);

    confidence_threshold_ =
        config["postprocess"]["confidence_threshold"].as<float>();

    nms_threshold_ =
        config["postprocess"]["nms_threshold"].as<float>();

    Logger::debug(
        "[Postprocess] 신뢰도 임계값: " +
        std::to_string(confidence_threshold_)
    );

    Logger::debug(
        "[Postprocess] NMS 임계값: " +
        std::to_string(nms_threshold_)
    );
}


// YOLOv8 Raw Output을 Detection 목록으로 변환한다.
std::vector<Detection>
YoloV8Postprocessor::postprocess(const ModelOutput& output)
{
    (void)output;

    std::vector<Detection> detections;

    // TODO:
    // 1. ModelOutput의 Raw Tensor 접근
    // 2. YOLOv8 Output Decode
    // 3. Confidence Threshold 적용
    // 4. Bounding Box 생성
    // 5. NMS 적용
    // 6. Detection 목록 생성

    return detections;
}
