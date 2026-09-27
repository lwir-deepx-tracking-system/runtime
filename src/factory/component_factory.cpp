#include "factory/component_factory.hpp"

#include <stdexcept>

#include "detection/deepx_yolov8_detector.hpp"
#include "tracking/bytetrack_tracker.hpp"

std::unique_ptr<Detector>
ComponentFactory::create_detector(const AppConfig& config)
{
    if (config.detector.backend == "deepx" &&
        config.detector.type == "yolov8")
    {
        return std::make_unique<DeepxYoloV8Detector>(
            config.detector.config_path
        );
    }

    throw std::runtime_error(
        "지원하지 않는 Detection 조합: " +
        config.detector.backend + "/" + config.detector.type
    );
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
