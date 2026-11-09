#include "config/app_config.hpp"
#include "common/logger.hpp"
#include "gui/gui_protocol.hpp"

#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace {
// 개수만 제공하는 PC 계약을 위해 카탈로그 ID를 1..count로 제한한다.
template<class Entry>
void validate_catalog_ids(const std::map<int, Entry>& catalog, const char* name)
{
    int expected = 1;
    for (const auto& item : catalog)
        if (item.first != expected++)
            throw std::runtime_error(std::string(name) + " IDs must be consecutive from 1");
}

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

// 선택 키가 없으면 기본값을 쓴다. 키가 있는데 타입이 맞지 않으면 경로가 포함된 오류를 만든다.
template <typename T>
T optional_value(const YAML::Node& node, const char* key, const std::string& path, const T& fallback)
{
    if (!node || !node[key]) return fallback;
    try { return node[key].as<T>(); }
    catch (const YAML::Exception& e) {
        throw std::runtime_error("invalid configuration: " + path + "." + key + " (" + e.what() + ")");
    }
}
PostprocessOutputFormat parse_postprocess_output_format(const std::string& value)
{
    if (value == "auto") return PostprocessOutputFormat::Auto;
    if (value == "decoded_xywh") return PostprocessOutputFormat::DecodedXywh;
    if (value == "end_to_end_xyxy") return PostprocessOutputFormat::EndToEndXyxy;
    if (value == "split_head") return PostprocessOutputFormat::SplitHead;
    throw std::runtime_error(
        "postprocess.output_format must be one of: auto, decoded_xywh, "
        "end_to_end_xyxy, split_head");
}

PostprocessScoreActivation parse_postprocess_score_activation(
    const std::string& value)
{
    if (value == "none") return PostprocessScoreActivation::None;
    if (value == "sigmoid") return PostprocessScoreActivation::Sigmoid;
    throw std::runtime_error(
        "postprocess.split_class_score_activation must be one of: none, sigmoid");
}

PostprocessNmsMode parse_postprocess_nms_mode(const std::string& value)
{
    if (value == "auto") return PostprocessNmsMode::Auto;
    if (value == "enabled") return PostprocessNmsMode::Enabled;
    if (value == "disabled") return PostprocessNmsMode::Disabled;
    throw std::runtime_error(
        "postprocess.nms_mode must be one of: auto, enabled, disabled");
}
}  // namespace

bool BenchmarkSequenceConfig::contains(const std::string& sequence_id) const
{
    return !range_enabled || (sequence_id >= start && sequence_id <= end);
}

namespace config_parser {

// yaml-cpp 예외가 파일 경로를 잃지 않도록 runtime 오류로 변환한다.
YAML::Node load_yaml(const std::string& path)
{
    try { return YAML::LoadFile(path); }
    catch (const YAML::Exception& e) {
        throw std::runtime_error("cannot read configuration " + path + ": " + e.what());
    }
}

std::string required_string(
    const YAML::Node& node,
    const char* key,
    const std::string& path)
{
    return required<std::string>(node, key, path);
}

GuiVideoCodec parse_gui_video_codec(const std::string& value)
{
    if (value == "h264") return GuiVideoCodec::H264;
    if (value == "jpeg") return GuiVideoCodec::JPEG;
    throw std::runtime_error("gui.video.codec must be jpeg or h264");
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

    c.buffer_count = required<int>(
        node, "buffer_count", "detection");
    if (c.buffer_count < 0 || c.buffer_count > 100)
        throw std::runtime_error(
            "detection.buffer_count must be 0 (auto) or in [1, 100]");

    c.use_ort = required<bool>(node, "use_ort", "detection");
    return c;
}

TrackingConfig load_tracking_config(const YAML::Node& node)
{
    TrackingConfig c;
    const std::string type = optional_value<std::string>(
        node, "type", "tracking", "bytetrack");
    if (type == "bytetrack")
        c.type = TrackerType::ByteTrack;
    else
        throw std::runtime_error("tracking.type is unsupported: " + type);
    c.track_threshold = required<float>(
        node, "track_threshold", "tracking");
    c.match_threshold = required<float>(
        node, "match_threshold", "tracking");
    c.track_buffer = required<int>(
        node, "track_buffer", "tracking");

    // 선택 키: 생략하면 기능이 꺼진 기본값(세부 임계값은 공식 ByteTrack 값)을 쓴다.
    c.low_threshold = optional_value<float>(
        node, "low_threshold", "tracking", c.low_threshold);
    c.new_track_threshold = optional_value<float>(
        node, "new_track_threshold", "tracking", c.new_track_threshold);
    c.low_match_threshold = optional_value<float>(
        node, "low_match_threshold", "tracking", c.low_match_threshold);
    c.unconfirmed_match_threshold = optional_value<float>(
        node, "unconfirmed_match_threshold", "tracking", c.unconfirmed_match_threshold);
    c.fuse_score = optional_value<bool>(
        node, "fuse_score", "tracking", c.fuse_score);
    c.nsa_kalman = optional_value<bool>(
        node, "nsa_kalman", "tracking", c.nsa_kalman);
    c.kalman_format = optional_value<std::string>(
        node, "kalman_format", "tracking", c.kalman_format);
    c.lost_output_frames = optional_value<int>(
        node, "lost_output_frames", "tracking", c.lost_output_frames);
    c.buffer_scale = optional_value<float>(
        node, "buffer_scale", "tracking", c.buffer_scale);
    c.buffer_max_px = optional_value<float>(
        node, "buffer_max_px", "tracking", c.buffer_max_px);
    c.buffer_small_size = optional_value<float>(
        node, "buffer_small_size", "tracking", c.buffer_small_size);
    c.buffer_snap = optional_value<bool>(
        node, "buffer_snap", "tracking", c.buffer_snap);
    c.oru = optional_value<bool>(
        node, "oru", "tracking", c.oru);
    c.ocm_weight = optional_value<float>(
        node, "ocm_weight", "tracking", c.ocm_weight);
    c.ocm_delta_t = optional_value<int>(
        node, "ocm_delta_t", "tracking", c.ocm_delta_t);
    c.ocr = optional_value<bool>(
        node, "ocr", "tracking", c.ocr);

    if (c.track_threshold <= 0.0F || c.track_threshold > 1.0F)
        throw std::runtime_error("tracking.track_threshold must be in (0, 1]");
    if (c.match_threshold <= 0.0F || c.match_threshold > 1.0F)
        throw std::runtime_error("tracking.match_threshold must be in (0, 1]");
    if (c.track_buffer <= 0)
        throw std::runtime_error("tracking.track_buffer must be positive");
    if (c.low_threshold < 0.0F || c.low_threshold >= c.track_threshold)
        throw std::runtime_error("tracking.low_threshold must be in [0, track_threshold)");
    if (c.new_track_threshold != 0.0F &&
        (c.new_track_threshold < c.track_threshold || c.new_track_threshold > 1.0F))
        throw std::runtime_error(
            "tracking.new_track_threshold must be 0 (auto) or in [track_threshold, 1]");
    if (c.low_match_threshold <= 0.0F || c.low_match_threshold > 1.0F)
        throw std::runtime_error("tracking.low_match_threshold must be in (0, 1]");
    if (c.unconfirmed_match_threshold <= 0.0F || c.unconfirmed_match_threshold > 1.0F)
        throw std::runtime_error("tracking.unconfirmed_match_threshold must be in (0, 1]");
    if (c.lost_output_frames < 0)
        throw std::runtime_error("tracking.lost_output_frames must not be negative");
    if (c.kalman_format != "xyah" && c.kalman_format != "xywh")
        throw std::runtime_error("tracking.kalman_format must be xyah or xywh");
    if (c.buffer_scale < 0.0F || c.buffer_scale > 2.0F)
        throw std::runtime_error("tracking.buffer_scale must be in [0, 2]");
    if (c.buffer_max_px < 0.0F)
        throw std::runtime_error("tracking.buffer_max_px must not be negative");
    if (c.buffer_small_size < 0.0F)
        throw std::runtime_error("tracking.buffer_small_size must not be negative");
    if (!std::isfinite(c.ocm_weight) || c.ocm_weight < 0.0F || c.ocm_weight > 1.0F)
        throw std::runtime_error("tracking.ocm_weight must be in [0, 1]");
    if (c.ocm_delta_t < 1 || c.ocm_delta_t > 30)
        throw std::runtime_error("tracking.ocm_delta_t must be in [1, 30]");
    return c;
}

ControlConfig load_control_config(const YAML::Node& node)
{
    ControlConfig c;
    c.enabled = required<bool>(node, "enabled", "control");
    c.heartbeat_timeout_ms = node["heartbeat_timeout_ms"].as<int>(1500);
    c.poll_interval_ms = node["poll_interval_ms"].as<int>(25);
    const auto gimbal = node["gimbal"];
    if (gimbal) {
        c.gimbal.device = gimbal["device"].as<std::string>("/dev/ttyUSB0");
        c.gimbal.baud_rate = gimbal["baud_rate"].as<int>(115200);
        c.gimbal.write_timeout_ms = gimbal["write_timeout_ms"].as<int>(100);
        c.gimbal.speed = gimbal["speed"].as<int>(c.gimbal.speed);
        c.gimbal.speed_limit = gimbal["speed_limit"].as<int>(c.gimbal.speed_limit);
        c.gimbal.acceleration = gimbal["acceleration"].as<int>(50);
        c.gimbal.home_x_deg = gimbal["home_x_deg"].as<int>(0);
        c.gimbal.home_y_deg = gimbal["home_y_deg"].as<int>(0);
        c.gimbal.x_sign = gimbal["x_sign"].as<int>(1);
        c.gimbal.y_sign = gimbal["y_sign"].as<int>(1);
    }
    const auto automatic = node["auto"];
    if (automatic) {
#define AUTO_VALUE(name) c.automatic.name = automatic[#name].as<decltype(c.automatic.name)>(c.automatic.name)
        AUTO_VALUE(deadband_x); AUTO_VALUE(deadband_y);
        AUTO_VALUE(fov_x_deg); AUTO_VALUE(fov_y_deg); AUTO_VALUE(min_confidence);
        AUTO_VALUE(speed_min); AUTO_VALUE(speed_max); AUTO_VALUE(speed_rate);
        AUTO_VALUE(period_ms); AUTO_VALUE(input_timeout_ms); AUTO_VALUE(reacquire_timeout_ms);
#undef AUTO_VALUE
    }
    if (automatic) {
        const auto type = automatic["controller"].as<std::string>("p");
        if (type == "p") c.automatic.controller = AutoControllerType::P;
        else if (type == "pi") c.automatic.controller = AutoControllerType::PI;
        else if (type == "pd") c.automatic.controller = AutoControllerType::PD;
        else if (type == "pid") c.automatic.controller = AutoControllerType::PID;
        else throw std::runtime_error("control.auto.controller must be p, pi, pd or pid");
        // Old YAML with a common kp remains valid; axis settings override it.
        const double common_kp = automatic["kp"].as<double>(30.0);
        if (!std::isfinite(common_kp) || common_kp <= 0)
            throw std::runtime_error("control.auto.kp must be finite and positive");
        const auto load_axis = [common_kp](const YAML::Node& node) {
            PidAxisConfig axis;
            axis.kp = common_kp;
            if (node) {
                if (!node.IsMap()) throw std::runtime_error("control.auto PID axis must be a map");
                axis.kp = node["kp"].as<double>(axis.kp);
                axis.ki = node["ki"].as<double>(axis.ki);
                axis.kd = node["kd"].as<double>(axis.kd);
                axis.integral_limit = node["integral_limit"].as<double>(axis.integral_limit);
                axis.derivative_filter_ms = node["derivative_filter_ms"].as<double>(axis.derivative_filter_ms);
            }
            if (!std::isfinite(axis.kp) || axis.kp <= 0 ||
                !std::isfinite(axis.ki) || axis.ki < 0 ||
                !std::isfinite(axis.kd) || axis.kd < 0 ||
                !std::isfinite(axis.integral_limit) || axis.integral_limit < 0 ||
                !std::isfinite(axis.derivative_filter_ms) || axis.derivative_filter_ms <= 0)
                throw std::runtime_error("control.auto PID gains/limits/filter are invalid");
            return axis;
        };
        c.automatic.pid_x = load_axis(automatic["pid_x"]);
        c.automatic.pid_y = load_axis(automatic["pid_y"]);
    }
    const auto& a = c.automatic;
    if (!std::isfinite(a.deadband_x) || a.deadband_x < 0 ||
        !std::isfinite(a.deadband_y) || a.deadband_y < 0 ||
        !std::isfinite(a.fov_x_deg) || !std::isfinite(a.fov_y_deg) ||
        !((a.fov_x_deg == 0 && a.fov_y_deg == 0) ||
          (a.fov_x_deg > 0 && a.fov_x_deg < 180 && a.fov_y_deg > 0 && a.fov_y_deg < 180)) ||
        !std::isfinite(a.min_confidence) || a.min_confidence < 0 || a.min_confidence > 1 ||
        a.speed_min < 1 || a.speed_max < a.speed_min || a.speed_max > c.gimbal.speed_limit ||
        !std::isfinite(a.speed_rate) || a.speed_rate <= 0 ||
        a.period_ms < 1 || a.period_ms > 1000 ||
        a.input_timeout_ms < a.period_ms || a.input_timeout_ms > 60000 ||
        a.reacquire_timeout_ms < a.input_timeout_ms || a.reacquire_timeout_ms > 60000)
        throw std::runtime_error("control.auto settings are invalid (speed_max must not exceed gimbal.speed_limit)");
    if (c.heartbeat_timeout_ms < 100 || c.heartbeat_timeout_ms > 60000 ||
        c.poll_interval_ms < 1 || c.poll_interval_ms > c.heartbeat_timeout_ms / 2)
        throw std::runtime_error("control heartbeat/poll interval is invalid");
    if (c.gimbal.device.empty() || c.gimbal.write_timeout_ms < 1 ||
        c.gimbal.write_timeout_ms > c.heartbeat_timeout_ms / 2)
        throw std::runtime_error("control.gimbal device/write timeout is invalid");
    if (c.gimbal.baud_rate != 9600 && c.gimbal.baud_rate != 57600 && c.gimbal.baud_rate != 115200)
        throw std::runtime_error("control.gimbal.baud_rate must be 9600, 57600 or 115200");
    if (c.gimbal.speed_limit < 1 || c.gimbal.speed < 1 ||
        c.gimbal.speed > c.gimbal.speed_limit || c.gimbal.acceleration < 1 ||
        c.gimbal.home_x_deg < -180 || c.gimbal.home_x_deg > 180 ||
        c.gimbal.home_y_deg < -30 || c.gimbal.home_y_deg > 90 ||
        (c.gimbal.x_sign != 1 && c.gimbal.x_sign != -1) ||
        (c.gimbal.y_sign != 1 && c.gimbal.y_sign != -1))
        throw std::runtime_error("control.gimbal speed/acceleration/home/axis setting is invalid");
    return c;
}

MeasurementConfig load_measurement_config(const YAML::Node& node)
{
    MeasurementConfig c;
    c.enabled = required<bool>(node, "enabled", "measurement");
    c.output_root = required<std::string>(node, "output_root", "measurement");
    if (c.output_root.empty())
        throw std::runtime_error("measurement.output_root must not be empty");
    return c;
}

GuiConfig load_gui_config(const YAML::Node& node)
{
    // YAML 파싱과 범위 검증을 여기서 끝내 GUI 컴포넌트에는 typed 설정만 전달한다.
    GuiConfig c{};
    c.enabled = required<bool>(node, "enabled", "gui");

    // 영상 전송 설정: Orange Pi -> GUI 방향의 JPEG or H.264/RTP/UDP 채널이다.
    const YAML::Node video = node["video"];
    c.video.host = required<std::string>(video, "host", "gui.video");
    c.video.port = parse_port(video, "port", "gui.video");
    c.video.codec = parse_gui_video_codec(
        required<std::string>(video, "codec", "gui.video"));
    c.video.encoder = required<std::string>(video, "encoder", "gui.video");
    c.video.bitrate_kbps = required<int>(video, "bitrate_kbps", "gui.video");
    c.video.jpeg_quality = video["jpeg_quality"].as<int>(80);
    c.video.keyframe_interval = video["keyframe_interval"].as<int>(30);
    c.video.fps = required<int>(video, "fps", "gui.video");
    c.video.rtp_mtu = required<int>(video, "rtp_mtu", "gui.video");
    if (c.video.host.empty())
        throw std::runtime_error("gui.video.host must not be empty");
    const bool jpeg = c.video.codec == GuiVideoCodec::JPEG;
    if ((jpeg && c.video.encoder != "mppjpegenc" && c.video.encoder != "jpegenc") ||
        (!jpeg && c.video.encoder != "mpph264enc" && c.video.encoder != "x264enc"))
        throw std::runtime_error("gui.video.encoder does not match codec (MPP or explicit software encoder required)");
    if (c.video.bitrate_kbps <= 0 || c.video.bitrate_kbps > 2147483)
        throw std::runtime_error("gui.video.bitrate_kbps must be in [1, 2147483]");
    if (c.video.jpeg_quality < 1 || c.video.jpeg_quality > 99)
        throw std::runtime_error("gui.video.jpeg_quality must be in [1, 99]");
    if (c.video.keyframe_interval < 1)
        throw std::runtime_error("gui.video.keyframe_interval must be positive");
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

    // 명령 설정: PC 명령과 보드 응답을 위한 양방향 TCP 채널이다.
    const YAML::Node command = node["command"];
    const auto handler = command["handler"].as<std::string>("deferred");
    if (handler == "deferred") c.command.handler = GuiCommandHandlerType::Deferred;
    else if (handler == "tracking") c.command.handler = GuiCommandHandlerType::Tracking;
    else throw std::runtime_error("gui.command.handler must be deferred or tracking");
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

BenchmarkPathConfig load_benchmark_path_config(const YAML::Node& node)
{
    BenchmarkPathConfig c{};

    c.dataset_root = required<std::string>(
        node, "dataset_root", "path");

    c.output_root = required<std::string>(
        node, "output_root", "path");

    if (c.dataset_root.empty())
        throw std::runtime_error("path.dataset_root must not be empty");

    if (c.output_root.empty())
        throw std::runtime_error("path.output_root must not be empty");

    return c;
}

BenchmarkSequenceConfig load_benchmark_sequence_config(const YAML::Node& node)
{
    BenchmarkSequenceConfig c{};

    c.range_enabled = required<bool>(
        node, "range_enabled", "sequence");
    c.start = required<std::string>(node, "start", "sequence");
    c.end = required<std::string>(node, "end", "sequence");

    if (c.range_enabled)
    {
        if (c.start.empty())
            throw std::runtime_error("sequence.start must not be empty when range is enabled");
        if (c.end.empty())
            throw std::runtime_error("sequence.end must not be empty when range is enabled");
        if (c.start > c.end)
            throw std::runtime_error("sequence.start must not be greater than sequence.end");
    }

    return c;
}

CaptureDatasetConfig load_capture_dataset_config(const YAML::Node& node)
{
    CaptureDatasetConfig c{};

    c.root = required<std::string>(
        node, "root", "dataset");

    if (c.root.empty())
        throw std::runtime_error("dataset.root must not be empty");

    return c;
}

CaptureSequenceConfig load_capture_sequence_config(const YAML::Node& node)
{
    CaptureSequenceConfig c{};

    c.id = required<std::string>(
        node, "id", "sequence");

    if (c.id.empty())
        throw std::runtime_error("sequence.id must not be empty");

    return c;
}

CaptureConditionConfig load_capture_condition_config(const YAML::Node& node)
{
    CaptureConditionConfig c{};

    c.distance_m = required<float>(
        node, "distance_m", "condition");

    c.person_count = required<int>(
        node, "person_count", "condition");

    c.motion = required<std::string>(
        node, "motion", "condition");

    c.temperature_c = required<float>(
        node, "temperature_c", "condition");

    if (c.distance_m <= 0.0F)
        throw std::runtime_error(
            "condition.distance_m must be positive");

    if (c.person_count <= 0)
        throw std::runtime_error(
            "condition.person_count must be positive");

    if (c.motion.empty())
        throw std::runtime_error(
            "condition.motion must not be empty");

    return c;
}

}  // namespace config_parser

static ModelPostprocessConfig parse_postprocess_config(const YAML::Node& post);

// 모델 YAML 전체를 typed config로 변환한다.
ModelConfig load_model_config(const std::string& path)
{
    const YAML::Node root = config_parser::load_yaml(path);
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
    c.preprocess.type = required<std::string>(root["preprocess"], "type", "preprocess");
    c.preprocess.resize = required<std::string>(root["preprocess"], "resize", "preprocess");
    c.preprocess.normalize = required<bool>(root["preprocess"], "normalize", "preprocess");
    c.preprocess.pad_value = required<int>(root["preprocess"], "pad_value", "preprocess");
    c.postprocess = parse_postprocess_config(root["postprocess"]);
    // 현재 LWIR 전처리 구현이 안전하게 처리할 수 있는 계약만 허용한다.
    if (c.camera_input.opencv_type != "CV_16UC1") throw std::runtime_error("camera_input.opencv_type must be CV_16UC1");
    if (c.camera_input.width <= 0 || c.camera_input.height <= 0 || c.input.width <= 0 || c.input.height <= 0)
        throw std::runtime_error("camera and model dimensions must be positive");
    if (clip_min < 0 || clip_max > std::numeric_limits<std::uint16_t>::max() || clip_min >= clip_max)
        throw std::runtime_error("camera_input clip range must satisfy 0 <= clip_min < clip_max <= 65535");
    c.camera_input.clip_min = static_cast<std::uint16_t>(clip_min);
    c.camera_input.clip_max = static_cast<std::uint16_t>(clip_max);
    if (c.camera_input.channel_mode != "replicate_gray_to_rgb") throw std::runtime_error("unsupported camera_input.channel_mode");
    if (c.preprocess.type != "lwir_letterbox") throw std::runtime_error("unsupported preprocess.type");
    if (c.preprocess.resize != "letterbox" || c.preprocess.normalize)
        throw std::runtime_error("only letterbox with normalize=false is supported");
    if (c.preprocess.pad_value < 0 || c.preprocess.pad_value > 255)
        throw std::runtime_error("preprocess.pad_value must be in [0, 255]");
    return c;
}

// 모델 YAML의 후처리 계약을 파싱하고 출력 해석 방식·필터링 조건을 검증한다.
static ModelPostprocessConfig parse_postprocess_config(const YAML::Node& post)
{
    ModelPostprocessConfig c;
    c.type = required<std::string>(post, "type", "postprocess");
    c.output_format = parse_postprocess_output_format(
        required<std::string>(post, "output_format", "postprocess"));
    c.split_output_levels = required<int>(post, "split_output_levels", "postprocess");
    c.split_regression_bins = required<int>(post, "split_regression_bins", "postprocess");
    c.split_class_score_activation = parse_postprocess_score_activation(
        required<std::string>(post, "split_class_score_activation", "postprocess"));
    c.nms_mode = parse_postprocess_nms_mode(
        required<std::string>(post, "nms_mode", "postprocess"));
    c.class_agnostic_nms = required<bool>(post, "class_agnostic_nms", "postprocess");
    c.confidence_threshold = required<float>(post, "confidence_threshold", "postprocess");
    c.nms_threshold = required<float>(post, "nms_threshold", "postprocess");
    c.num_classes = required<int>(post, "num_classes", "postprocess");
    c.class_names = required<std::vector<std::string>>(post, "class_names", "postprocess");

    if (c.type != "yolov8" && c.type != "yolo26")
        throw std::runtime_error("postprocess.type must be yolov8 or yolo26");
    if (c.split_output_levels <= 0 || c.split_output_levels > 16)
        throw std::runtime_error("postprocess.split_output_levels must be in [1, 16]");
    if (c.split_regression_bins <= 0 || c.split_regression_bins > 256)
        throw std::runtime_error("postprocess.split_regression_bins must be in [1, 256]");
    if (!std::isfinite(c.confidence_threshold) ||
        c.confidence_threshold < 0.0F || c.confidence_threshold > 1.0F)
        throw std::runtime_error("postprocess.confidence_threshold must be in [0, 1]");
    if (!std::isfinite(c.nms_threshold) ||
        c.nms_threshold < 0.0F || c.nms_threshold > 1.0F)
        throw std::runtime_error("postprocess.nms_threshold must be in [0, 1]");
    if (c.num_classes <= 0 || static_cast<int>(c.class_names.size()) != c.num_classes)
        throw std::runtime_error("postprocess.class_names size must equal num_classes");
    return c;
}

// 트래커 전용 YAML을 읽어 알고리즘별 설정을 검증한다.
TrackingConfig load_tracker_config(const std::string& path)
{
    const YAML::Node tracking = config_parser::load_yaml(path)["tracking"];
    (void)config_parser::required_string(tracking, "type", "tracking");
    return config_parser::load_tracking_config(tracking);
}

// 카탈로그의 ID 중복과 설정 파일을 검증해 추적 설정을 준비한다.
std::map<int, TrackingCatalogEntry> config_parser::load_tracking_catalog(
    const YAML::Node& node)
{
    if (!node || !node.IsSequence() || node.size() == 0)
        throw std::runtime_error("tracking_catalog must be a nonempty sequence");
    std::map<int, TrackingCatalogEntry> catalog;
    for (const YAML::Node& item : node)
    {
        TrackingCatalogEntry entry;
        entry.id = required<int>(item, "id", "tracking_catalog");
        entry.config_path = required_string(item, "config", "tracking_catalog");
        if (entry.id <= 0 || entry.config_path.empty())
            throw std::runtime_error("tracking_catalog entries need a positive id and config path");
        entry.config = load_tracker_config(entry.config_path);
        if (!catalog.emplace(entry.id, std::move(entry)).second)
            throw std::runtime_error("Duplicate tracking_catalog id");
    }
    validate_catalog_ids(catalog, "tracking_catalog");
    return catalog;
}

// 실행 설정의 tracking.id를 카탈로그에서 찾아 검증된 설정을 돌려준다.
int config_parser::select_tracking_id(
    const YAML::Node& node, const std::map<int, TrackingCatalogEntry>& catalog)
{
    if (!node || !node.IsMap() || node.size() != 1)
        throw std::runtime_error("tracking must contain only the selected id");
    const int id = required<int>(node, "id", "tracking");
    const auto it = catalog.find(id);
    if (it == catalog.end())
        throw std::runtime_error("Unknown tracking.id=" + std::to_string(id));
    return id;
}

TrackingConfig config_parser::select_tracking_config(
    const YAML::Node& node, const std::map<int, TrackingCatalogEntry>& catalog)
{
    return catalog.at(select_tracking_id(node, catalog)).config;
}

// runtime 설정을 읽고 별도 모델 YAML까지 한 번에 준비한다.
RuntimeConfig load_runtime_config(const std::string& path)
{
    const YAML::Node root = config_parser::load_yaml(path);
    RuntimeConfig c;
    c.logging = config_parser::load_logging_config(root["logging"]);

    c.model_config_path = config_parser::required_string(
        root["model"], "config", "model");
    if (c.model_config_path.empty())
        throw std::runtime_error("model.config must not be empty");
    c.model = load_model_config(c.model_config_path);

    const YAML::Node model_catalog = root["model_catalog"];
    if (model_catalog && !model_catalog.IsSequence())
        throw std::runtime_error("model_catalog must be a sequence");
    if (model_catalog)
    {
        for (const YAML::Node& item : model_catalog)
        {
            ModelCatalogEntry entry;
            entry.id = item["id"].as<int>();
            entry.config_path = config_parser::required_string(
                item, "config", "model_catalog");
            if (entry.id <= 0 || entry.config_path.empty())
                throw std::runtime_error(
                    "model_catalog entries need a positive id and config path");
            entry.model = load_model_config(entry.config_path);
            if (!c.model_catalog.emplace(entry.id, std::move(entry)).second)
                throw std::runtime_error("Duplicate model_catalog id");
        }
    }
    if (c.model_catalog.empty())
    {
        ModelCatalogEntry entry;
        entry.id = 1;
        entry.config_path = c.model_config_path;
        entry.model = c.model;
        c.model_catalog.emplace(entry.id, std::move(entry));
    }

    validate_catalog_ids(c.model_catalog, "model_catalog");
    const std::string active_config =
        fs::path(c.model_config_path).lexically_normal().string();
    for (const auto& item : c.model_catalog)
    {
        if (fs::path(item.second.config_path).lexically_normal().string() ==
            active_config)
            c.initial_model_id = item.first;
    }
    if (c.initial_model_id == 0)
        throw std::runtime_error(
            "model.config must be listed in model_catalog");
    c.model = c.model_catalog.at(c.initial_model_id).model;

    c.detection = config_parser::load_detection_config(root["detection"]);
    c.tracking_catalog = config_parser::load_tracking_catalog(root["tracking_catalog"]);
    c.initial_tracker_id = config_parser::select_tracking_id(
        root["tracking"], c.tracking_catalog);
    c.tracking = c.tracking_catalog.at(c.initial_tracker_id).config;
    c.control = config_parser::load_control_config(root["control"]);
    c.measurement = config_parser::load_measurement_config(root["measurement"]);
    c.gui = config_parser::load_gui_config(root["gui"]);

    // Camera and display conversion remain active while the inference model changes.
    for (const auto& [id, entry] : c.model_catalog)
    {
        const auto& camera = entry.model.camera_input;
        const auto& initial = c.model.camera_input;
        if (camera.width != initial.width || camera.height != initial.height ||
            camera.opencv_type != initial.opencv_type ||
            camera.channel_mode != initial.channel_mode ||
            camera.clip_min != initial.clip_min || camera.clip_max != initial.clip_max)
            throw std::runtime_error("model_catalog camera input differs for model id=" +
                std::to_string(id));
    }
    if (c.gui.enabled)
    {
        if (c.model.camera_input.width != static_cast<int>(kGuiFrameWidth) ||
            c.model.camera_input.height != static_cast<int>(kGuiFrameHeight))
            throw std::runtime_error("Camera dimensions do not match the configured GUI protocol");
    }

    const bool tracking_handler = c.gui.command.handler == GuiCommandHandlerType::Tracking;
    // 비활성 GUI/명령 구역의 handler 선택은 제어 활성화를 요구하지 않는다.
    const bool active_tracking_handler = c.gui.enabled && c.gui.command.enabled && tracking_handler;
    if ((active_tracking_handler && !c.control.enabled) ||
        (c.control.enabled && (!c.gui.enabled || !c.gui.command.enabled || !tracking_handler)))
        throw std::runtime_error("Gimbal control requires control.enabled=true, GUI command enabled and handler=tracking");

    return c;
}
