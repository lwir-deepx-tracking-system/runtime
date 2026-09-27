#include "app/app_config.hpp"
#include "common/logger.hpp"

#include <limits>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

namespace {
template <typename T>
T required(const YAML::Node& node, const char* key, const std::string& path)
{
    if (!node || !node[key]) throw std::runtime_error("missing configuration: " + path + "." + key);
    try { return node[key].as<T>(); }
    catch (const YAML::Exception& e) {
        throw std::runtime_error("invalid configuration: " + path + "." + key + " (" + e.what() + ")");
    }
}

YAML::Node load_yaml(const std::string& path)
{
    try { return YAML::LoadFile(path); }
    catch (const YAML::Exception& e) {
        throw std::runtime_error("cannot read configuration " + path + ": " + e.what());
    }
}
}

ModelConfig load_model_config(const std::string& path)
{
    const YAML::Node root = load_yaml(path);
    ModelConfig c;
    c.name = required<std::string>(root["model"], "name", "model");
    c.path = required<std::string>(root["model"], "path", "model");
    const YAML::Node camera = root["camera_input"];
    c.camera_input.opencv_type = required<std::string>(camera, "opencv_type", "camera_input");
    c.camera_input.width = required<int>(camera, "width", "camera_input");
    c.camera_input.height = required<int>(camera, "height", "camera_input");
    const int clip_min = required<int>(camera, "clip_min", "camera_input");
    const int clip_max = required<int>(camera, "clip_max", "camera_input");
    c.camera_input.channel_mode = required<std::string>(camera, "channel_mode", "camera_input");
    c.input.width = required<int>(root["input"], "width", "input");
    c.input.height = required<int>(root["input"], "height", "input");
    c.preprocess.resize = required<std::string>(root["preprocess"], "resize", "preprocess");
    c.preprocess.normalize = required<bool>(root["preprocess"], "normalize", "preprocess");
    c.preprocess.pad_value = required<int>(root["preprocess"], "pad_value", "preprocess");
    const YAML::Node post = root["postprocess"];
    c.postprocess.confidence_threshold = required<float>(post, "confidence_threshold", "postprocess");
    c.postprocess.nms_threshold = required<float>(post, "nms_threshold", "postprocess");
    c.postprocess.num_classes = required<int>(post, "num_classes", "postprocess");
    c.postprocess.class_names = required<std::vector<std::string>>(post, "class_names", "postprocess");

    if (c.camera_input.opencv_type != "CV_16UC1") throw std::runtime_error("camera_input.opencv_type must be CV_16UC1");
    if (c.camera_input.width <= 0 || c.camera_input.height <= 0 || c.input.width <= 0 || c.input.height <= 0)
        throw std::runtime_error("camera and model dimensions must be positive");
    if (clip_min < 0 || clip_max > std::numeric_limits<std::uint16_t>::max() || clip_min >= clip_max)
        throw std::runtime_error("camera_input clip range must satisfy 0 <= clip_min < clip_max <= 65535");
    c.camera_input.clip_min = static_cast<std::uint16_t>(clip_min);
    c.camera_input.clip_max = static_cast<std::uint16_t>(clip_max);
    if (c.camera_input.channel_mode != "replicate_gray_to_rgb") throw std::runtime_error("unsupported camera_input.channel_mode");
    if (c.preprocess.resize != "letterbox" || c.preprocess.normalize)
        throw std::runtime_error("only letterbox with normalize=false is supported");
    if (c.preprocess.pad_value < 0 || c.preprocess.pad_value > 255)
        throw std::runtime_error("preprocess.pad_value must be in [0, 255]");
    if (c.postprocess.num_classes <= 0 || static_cast<int>(c.postprocess.class_names.size()) != c.postprocess.num_classes)
        throw std::runtime_error("postprocess.class_names size must equal num_classes");
    return c;
}

AppConfig load_config(const std::string& path)
{
    const YAML::Node root = load_yaml(path);
    AppConfig c;
    c.logging.level = required<std::string>(root["logging"], "level", "logging");
    Logger::set_level(c.logging.level);
    c.detector.type = required<std::string>(root["detector"], "type", "detector");
    c.detector.config_path = required<std::string>(root["detector"], "config", "detector");
    c.detection.backend = required<std::string>(root["detection"], "backend", "detection");
    const int max_inflight = required<int>(root["detection"], "max_inflight", "detection");
    if (max_inflight <= 0) throw std::runtime_error("detection.max_inflight must be positive");
    c.detection.max_inflight = static_cast<std::size_t>(max_inflight);
    c.tracking.type = required<std::string>(root["tracking"], "type", "tracking");
    c.tracking.config_path = required<std::string>(root["tracking"], "config", "tracking");
    c.control.enabled = required<bool>(root["control"], "enabled", "control");
    c.control.config_path = required<std::string>(root["control"], "config", "control");
    c.measurement.enabled = required<bool>(root["measurement"], "enabled", "measurement");
    if (c.detector.type != "yolov8" || c.detection.backend != "dx_app_async")
        throw std::runtime_error("only yolov8 with dx_app_async is supported");
    c.model = load_model_config(c.detector.config_path);
    Logger::info("[Config] runtime and model configuration loaded");
    return c;
}
