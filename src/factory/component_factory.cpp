#include "factory/component_factory.hpp"

#include <stdexcept>

#include "detection/dxapp_detection_pipeline.hpp"
#include "tracking/bytetrack_tracker.hpp"

std::unique_ptr<DetectionPipeline>
ComponentFactory::create_detection_pipeline(const AppConfig& config)
{
    if (config.detection.backend == "dx_app_async" && config.detector.type == "yolov8")
        return std::make_unique<DxAppDetectionPipeline>(config);
    throw std::runtime_error("unsupported detection pipeline");
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
