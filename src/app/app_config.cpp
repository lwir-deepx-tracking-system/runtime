#include "app/app_config.hpp"
#include "common/logger.hpp"

#include <limits>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

namespace {
// 필수 키가 없거나 요청한 타입으로 변환되지 않으면 경로가 포함된 오류를 만든다.
template <typename T>
T required(const YAML::Node& node, const char* key, const std::string& path)
{
    if (!node || !node[key]) throw std::runtime_error("missing configuration: " + path + "." + key);
    try { return node[key].as<T>(); }
    catch (const YAML::Exception& e) {
        throw std::runtime_error("invalid configuration: " + path + "." + key + " (" + e.what() + ")");
    }
}

// yaml-cpp 예외가 파일 경로를 잃지 않도록 runtime 오류로 변환한다.
YAML::Node load_yaml(const std::string& path)
{
    try { return YAML::LoadFile(path); }
    catch (const YAML::Exception& e) {
        throw std::runtime_error("cannot read configuration " + path + ": " + e.what());
    }
}

GuiVideoCodec parse_gui_video_codec(const std::string& value)
{
    if (value == "h264") return GuiVideoCodec::H264;
    throw std::runtime_error("only gui.video.codec=h264 is currently supported");
}

std::uint16_t parse_port(
    const YAML::Node& node,
    const char* key,
    const std::string& path)
{
    const int port = required<int>(node, key, path);
    if (port <= 0 || port > std::numeric_limits<std::uint16_t>::max())
        throw std::runtime_error(path + "." + key + " must be in [1, 65535]");
    return static_cast<std::uint16_t>(port);
}

LoggingConfig load_logging_config(const YAML::Node& node)
{
    LoggingConfig c;
    c.level = required<std::string>(node, "level", "logging");
    return c;
}

DetectionConfig load_detection_config(const YAML::Node& node)
{
    DetectionConfig c;
    const int max_inflight = required<int>(
        node, "max_inflight", "detection");
    if (max_inflight <= 0)
        throw std::runtime_error("detection.max_inflight must be positive");
    c.max_inflight = static_cast<std::size_t>(max_inflight);
    return c;
}

TrackingConfig load_tracking_config(const YAML::Node& node)
{
    TrackingConfig c;
    c.track_threshold = required<float>(
        node, "track_threshold", "tracking");
    c.match_threshold = required<float>(
        node, "match_threshold", "tracking");
    c.track_buffer = required<int>(
        node, "track_buffer", "tracking");

    if (c.track_threshold <= 0.0F || c.track_threshold > 1.0F)
        throw std::runtime_error("tracking.track_threshold must be in (0, 1]");
    if (c.match_threshold <= 0.0F || c.match_threshold > 1.0F)
        throw std::runtime_error("tracking.match_threshold must be in (0, 1]");
    if (c.track_buffer <= 0)
        throw std::runtime_error("tracking.track_buffer must be positive");
    return c;
}

ControlConfig load_control_config(const YAML::Node& node)
{
    ControlConfig c;
    c.enabled = required<bool>(node, "enabled", "control");
    c.driver = required<std::string>(node, "driver", "control");
    if (c.driver.empty())
        throw std::runtime_error("control.driver must not be empty");
    return c;
}

MeasurementConfig load_measurement_config(const YAML::Node& node)
{
    MeasurementConfig c;
    c.enabled = required<bool>(node, "enabled", "measurement");
    return c;
}

GuiConfig load_gui_config(const YAML::Node& node)
{
    // YAML 파싱과 범위 검증을 여기서 끝내 GUI 컴포넌트에는 typed 설정만 전달한다.
    GuiConfig c{};
    c.enabled = required<bool>(node, "enabled", "gui");

    // 영상 전송 설정: Orange Pi -> GUI 방향의 H.264/RTP/UDP 채널이다.
    const YAML::Node video = node["video"];
    c.video.host = required<std::string>(video, "host", "gui.video");
    c.video.port = parse_port(video, "port", "gui.video");
    c.video.codec = parse_gui_video_codec(
        required<std::string>(video, "codec", "gui.video"));
    c.video.encoder = required<std::string>(video, "encoder", "gui.video");
    c.video.bitrate_kbps = required<int>(video, "bitrate_kbps", "gui.video");
    c.video.fps = required<int>(video, "fps", "gui.video");
    c.video.rtp_mtu = required<int>(video, "rtp_mtu", "gui.video");
    if (c.video.host.empty())
        throw std::runtime_error("gui.video.host must not be empty");
    if (c.video.encoder != "x264enc")
        throw std::runtime_error(
            "only gui.video.encoder=x264enc is currently supported");
    if (c.video.bitrate_kbps <= 0)
        throw std::runtime_error("gui.video.bitrate_kbps must be positive");
    if (c.video.fps <= 0)
        throw std::runtime_error("gui.video.fps must be positive");
    if (c.video.rtp_mtu < 576 || c.video.rtp_mtu > 65507)
        throw std::runtime_error("gui.video.rtp_mtu must be in [576, 65507]");

    // Track metadata는 영상과 다른 UDP 포트로 보내 GUI가 원본 좌표 bbox를 그린다.
    const YAML::Node metadata = node["metadata"];
    c.metadata.enabled = required<bool>(metadata, "enabled", "gui.metadata");
    c.metadata.host = required<std::string>(metadata, "host", "gui.metadata");
    c.metadata.port = parse_port(metadata, "port", "gui.metadata");
    const int max_packet_bytes = required<int>(
        metadata, "max_packet_bytes", "gui.metadata");
    if (c.metadata.host.empty())
        throw std::runtime_error("gui.metadata.host must not be empty");
    if (max_packet_bytes < 64 || max_packet_bytes > 65507)
        throw std::runtime_error(
            "gui.metadata.max_packet_bytes must be in [64, 65507]");
    c.metadata.max_packet_bytes = static_cast<std::size_t>(max_packet_bytes);

    // 명령 설정: GUI -> Orange Pi 방향의 선택 track_id TCP 채널이다.
    const YAML::Node command = node["command"];
    c.command.enabled = required<bool>(command, "enabled", "gui.command");
    c.command.bind_address = required<std::string>(
        command, "bind_address", "gui.command");
    c.command.port = parse_port(command, "port", "gui.command");
    c.command.receive_timeout_ms = required<int>(
        command, "receive_timeout_ms", "gui.command");
    if (c.command.bind_address.empty())
        throw std::runtime_error("gui.command.bind_address must not be empty");
    if (c.command.receive_timeout_ms <= 0)
        throw std::runtime_error("gui.command.receive_timeout_ms must be positive");
    return c;
}
}

// 모델 YAML 전체를 typed config로 변환한다.
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

    // 현재 LWIR 전처리 구현이 안전하게 처리할 수 있는 계약만 허용한다.
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

// runtime 설정을 읽고 별도 모델 YAML까지 한 번에 준비한다.
AppConfig load_config(const std::string& path)
{
    const YAML::Node root = load_yaml(path);
    AppConfig c;
    c.logging = load_logging_config(root["logging"]);
    Logger::set_level(c.logging.level);

    c.model_config_path = required<std::string>(root["model"], "config", "model");
    if (c.model_config_path.empty())
        throw std::runtime_error("model.config must not be empty");
    c.model = load_model_config(c.model_config_path);

    c.detection = load_detection_config(root["detection"]);
    c.tracking = load_tracking_config(root["tracking"]);
    c.control = load_control_config(root["control"]);
    c.measurement = load_measurement_config(root["measurement"]);
    c.gui = load_gui_config(root["gui"]);
    Logger::info("[Config] runtime and model configuration loaded");
    return c;
}
