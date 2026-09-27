#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct LoggingConfig { std::string level; };
struct DetectorConfig { std::string type; std::string config_path; };
struct DetectionConfig { std::string backend; std::size_t max_inflight = 1; };
struct TrackingConfig { std::string type; std::string config_path; };
struct ControlConfig { bool enabled = false; std::string config_path; };
struct MeasurementConfig { bool enabled = false; };

struct CameraInputConfig {
    std::string opencv_type;
    int width = 0;
    int height = 0;
    std::uint16_t clip_min = 0;
    std::uint16_t clip_max = 0;
    std::string channel_mode;
};
struct ModelInputConfig { int width = 0; int height = 0; };
struct ModelPreprocessConfig { std::string resize; bool normalize = false; int pad_value = 114; };
struct ModelPostprocessConfig {
    float confidence_threshold = 0.25F;
    float nms_threshold = 0.45F;
    int num_classes = 0;
    std::vector<std::string> class_names;
};
struct ModelConfig {
    std::string name;
    std::string path;
    CameraInputConfig camera_input;
    ModelInputConfig input;
    ModelPreprocessConfig preprocess;
    ModelPostprocessConfig postprocess;
};

struct AppConfig {
    LoggingConfig logging;
    DetectorConfig detector;
    DetectionConfig detection;
    TrackingConfig tracking;
    ControlConfig control;
    MeasurementConfig measurement;
    ModelConfig model;
};

AppConfig load_config(const std::string& config_path);
ModelConfig load_model_config(const std::string& config_path);
