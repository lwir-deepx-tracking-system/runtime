#include "factory/component_factory.hpp"

#include <stdexcept>

#include "preprocess/yolov8_preprocessor.hpp"
#include "postprocess/yolov8_postprocessor.hpp"
#include "inference/inference.hpp"
#include "tracking/bytetrack_tracker.hpp"

// 현재 기준선의 YOLOv8 CPU 전처리 객체를 생성한다.

std::unique_ptr<Preprocessor> 
ComponentFactory::create_preprocessor(const AppConfig& config)
{
    if (config.preprocess.backend == "cpu" && config.detector.type == "yolov8")
    {
        return std::make_unique<YoloV8Preprocessor>(
            config.detector.config_path
        );
    }

    throw std::runtime_error("지원하지 않는 전처리 방식입니다");
}

// 설정된 Inference backend에 맞는 추론 구현체를 생성한다.
std::unique_ptr<Inference>
ComponentFactory::create_inference(const AppConfig& config)
{
    // DEEPX NPU 추론 사용
    if (config.inference.backend == "deepx")
    {
        // detector의 model config 경로를 Inference에 전달
        return std::make_unique<Inference>(
            config.detector.config_path
        );
    }

    // 지원하지 않는 backend가 설정된 경우
    throw std::runtime_error(
        "지원하지 않는 추론 backend: " +
        config.inference.backend
    );
}

// 현재 기준선의 YOLOv8 CPU 후처리 객체를 생성한다.
std::unique_ptr<Postprocessor>
ComponentFactory::create_postprocessor(const AppConfig& config)
{
    if (config.postprocess.backend == "cpu" && config.detector.type == "yolov8")
    {
        return std::make_unique<YoloV8Postprocessor>(
            config.detector.config_path
        );
    }

    throw std::runtime_error("지원하지 않는 후처리 방식입니다");
}

// 현재 기준선의 ByteTrack 객체를 생성한다.
std::unique_ptr<Tracker>
ComponentFactory::create_tracker(const AppConfig& config)
{
    if (config.tracking.type == "bytetrack")
    {
        return std::make_unique<ByteTrackTracker>(
            config.tracking.config_path
        );
    }

    throw std::runtime_error(
        "지원하지 않는 Tracker: " +
        config.tracking.type
    );
}
