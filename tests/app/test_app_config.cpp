#include "runtime/runtime.hpp"
#include "config/app_config.hpp"
#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <fstream>
#include <filesystem>
#include <unistd.h>

// 공용 YAML이 runtime에서 지원하는 Detection 및 LWIR 계약으로 파싱되는지 확인한다.
int main()
{
    const auto yaml = config_parser::load_yaml("config/runtime.yaml");
    const RuntimeConfig config = load_runtime_config("config/runtime.yaml");
    // Absolute top-level YAML must resolve the same models from another CWD.
    const auto original_cwd=std::filesystem::current_path();
    struct RestoreCwd {
        std::filesystem::path path;
        ~RestoreCwd() {std::filesystem::current_path(path);}
    } restore_cwd{original_cwd};
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    const auto relocated=load_runtime_config((original_cwd/"config/runtime.yaml").string());
    std::filesystem::current_path(original_cwd);
    if (relocated.detection.model.path!=config.detection.model.path ||
        relocated.reid.model.path!=config.reid.model.path ||
        relocated.tracking_catalog.at(2).config_path!=config.tracking_catalog.at(2).config_path ||
        relocated.tracking_catalog.at(6).config_path!=config.tracking_catalog.at(6).config_path)
        return 80;
    if (config.detection.model.path!=(original_cwd/"models/yolov8-n_640x640.dxnn").string() ||
        config.reid.model.path!=(original_cwd/"../model-bank/final_tracker/tracker/models/RVGGA0_REID2C256.dxnn").lexically_normal().string())
        return 81;

    // runtime.yaml의 모델 경로와 고정 Detection 실행 설정 검증.
    if (config.detection.config_path != std::filesystem::absolute("config/detection/yolov8n.yaml").lexically_normal().string() ||
        config.reid.config_path != std::filesystem::absolute("config/reid/repvgg.yaml").lexically_normal().string() ||
        config.reid.model.name != "repvgg" || config.reid.model.input_width != 64 ||
        config.reid.model.input_height != 128 || config.reid.model.output_dimensions != 128 ||
        config.reid.matcher_config_path != std::filesystem::absolute("config/reid/matcher.yaml").lexically_normal().string() ||
        config.reid.matcher.gallery_size != 5 ||
        config.reid.matcher.max_lost_frames != 90 ||
        config.reid.inference.max_inflight != 1 ||
        config.reid.inference.buffer_count != 0 ||
        config.reid.inference.use_ort ||
        config.detection.inference.max_inflight != 4 ||
        config.detection.inference.buffer_count != 6 ||
        config.detection.inference.use_ort != yaml["detection"]["inference"]["use_ort"].as<bool>()) return 1;

    // runtime 카탈로그가 ByteTrack YAML을 선택하고 실행 설정으로 합치는지 확인한다.
    if (config.tracking.track_threshold != 0.5F ||
        config.tracking.type != TrackerType::ByteTrack ||
        config.tracking.match_threshold != 0.8F ||
        config.tracking.track_buffer != 30 ||
        config.tracking.buffer_scale != 0.5F || config.tracking.buffer_snap ||
        config.tracking.fuse_score || config.tracking.nsa_kalman ||
        config.tracking.kalman_format != "xywh" || config.tracking.lost_output_frames != 0 ||
        config.tracking.oru || config.tracking.ocm_weight != 0.0F ||
        config.tracking.ocm_delta_t != 3 || !config.tracking.ocr ||
        config.tracking_catalog.size() != 6 ||
        config.tracking_catalog.at(1).config_path !=
            std::filesystem::absolute("config/tracking/bytetrack.yaml").lexically_normal().string() ||
        config.tracking_catalog.at(2).config.type != TrackerType::BotSort ||
        !config.tracking_catalog.at(3).config.post_reid ||
        !config.tracking_catalog.at(4).config.post_reid ||
        !config.tracking_catalog.at(4).config.class_gate ||
        !config.tracking_catalog.at(4).config.frame.gmc ||
        config.tracking_catalog.at(4).config.frame.warmth_min != 45000 ||
        config.tracking_catalog.at(4).config.edge_birth_threshold != 0.5F ||
        config.tracking_catalog.at(5).config.type != TrackerType::OcSort ||
        config.tracking_catalog.at(6).config.type != TrackerType::Sort ||
        config.control.enabled != yaml["control"]["enabled"].as<bool>() ||
        !config.measurement.enabled ||
        config.measurement.output_root != "/mnt/lwir_data/results/runtime")
        return 2;

    auto tracking = YAML::LoadFile("config/tracking/bytetrack.yaml")["tracking"];
    tracking["type"] = "unsupported";
    try {
        config_parser::load_tracking_config(tracking);
        return 41;
    } catch (const std::runtime_error&) {}
    tracking.remove("type");
    if (config_parser::load_tracking_config(tracking).type != TrackerType::ByteTrack)
        return 42;

    // 연결된 모델 YAML의 Camera와 모델 입력 계약 검증.
    if (config.detection.model.camera_input.opencv_type != "CV_16UC1") return 3;
    if (config.detection.model.camera_input.width != 640 || config.detection.model.camera_input.height != 480) return 4;
    if (config.detection.model.input.width != 640 || config.detection.model.input.height != 640) return 5;

    // 모델 YAML의 전처리와 output tensor 계약이 typed 설정으로 전달되는지 확인한다.
    if (config.detection.model.preprocess.type != "lwir_letterbox" ||
        config.detection.model.postprocess.type != "yolov8" ||
        config.detection.model.preprocess.pad_value != 114 ||
        config.detection.model.postprocess.output_format != PostprocessOutputFormat::Auto ||
        config.detection.model.postprocess.split_output_levels != 3 ||
        config.detection.model.postprocess.split_regression_bins != 16 ||
        config.detection.model.postprocess.split_class_score_activation !=
            PostprocessScoreActivation::Sigmoid ||
        config.detection.model.postprocess.nms_mode != PostprocessNmsMode::Enabled ||
        !config.detection.model.postprocess.class_agnostic_nms ||
        config.detection.model.postprocess.num_classes != 80)
        return 6;

    // YOLO26은 같은 parser를 사용하되 DFL-free split-head와 NMS auto 계약을 갖는다.
    const ModelConfig yolo26 = load_model_config("config/detection/yolo26s.yaml");
    if (yolo26.name != "yolo26s" ||
        yolo26.preprocess.type != "lwir_letterbox" ||
        yolo26.postprocess.type != "yolo26" ||
        yolo26.postprocess.output_format != PostprocessOutputFormat::Auto ||
        yolo26.postprocess.split_output_levels != 3 ||
        yolo26.postprocess.split_regression_bins != 1 ||
        yolo26.postprocess.split_class_score_activation !=
            PostprocessScoreActivation::Sigmoid ||
        yolo26.postprocess.nms_mode != PostprocessNmsMode::Auto ||
        !yolo26.postprocess.class_agnostic_nms ||
        yolo26.postprocess.num_classes != 16)
        return 7;
    if (config.detection_catalog.at(2).model.postprocess.type != "yolo26")
        return 44;
    if (!YAML::LoadFile("config/detection/yolov8n.yaml")["postprocess"] ||
        !YAML::LoadFile("config/detection/yolo26s.yaml")["postprocess"])
        return 47;

    // GUI 설정도 runtime.yaml에서 AppConfig를 통해 typed 값으로 전달된다.
    if (config.gui.enabled != yaml["gui"]["enabled"].as<bool>() ||
        config.gui.video.host != yaml["gui"]["video"]["host"].as<std::string>() ||
        config.gui.video.port != 5000 ||
        config.gui.video.codec != GuiVideoCodec::H264 ||
        config.gui.video.encoder != "mpph264enc" ||
        config.gui.video.bitrate_kbps != 3000 ||
        config.gui.video.fps != 30 ||
        config.gui.video.rtp_mtu != 1200 ||
        !config.gui.metadata.enabled ||
        config.gui.metadata.host != yaml["gui"]["metadata"]["host"].as<std::string>() ||
        config.gui.metadata.port != 5001 ||
        config.gui.metadata.max_packet_bytes != 1200 ||
        !config.gui.command.enabled ||
        config.gui.command.bind_address != yaml["gui"]["command"]["bind_address"].as<std::string>() ||
        config.gui.command.port != 5002 ||
        config.gui.command.receive_timeout_ms != 1000)
        return 8;
    auto gui = yaml["gui"];
    if (config.gui.video.jpeg_quality != 80 || config.gui.video.keyframe_interval != 30) return 9;
    gui["video"]["codec"] = "h264";
    gui["video"]["encoder"] = "mpph264enc";
    if (config_parser::load_gui_config(gui).video.codec != GuiVideoCodec::H264) return 10;
    const auto rejects = [&]() {
        try { config_parser::load_gui_config(gui); } catch (const std::runtime_error&) { return true; }
        return false;
    };
    gui["video"]["encoder"] = "mppjpegenc";
    if (!rejects()) return 11;
    gui["video"]["encoder"] = "mpph264enc";
    gui["video"]["jpeg_quality"] = 100;
    if (!rejects()) return 12;
    gui["video"]["jpeg_quality"] = 80;
    gui["video"]["keyframe_interval"] = 0;
    if (!rejects()) return 13;
    gui["video"]["keyframe_interval"] = 30;
    gui["command"]["handler"] = "auto_tracking";
    if (!rejects()) return 16;
    gui["command"]["handler"] = "deferred";
    auto typed = config_parser::load_gui_config(gui);
    if (typed.command.handler != GuiCommandHandlerType::Deferred) return 17;
    // The command handler selection is optional for existing YAML configurations.
    gui["command"].remove("handler");
    if (config_parser::load_gui_config(gui).command.handler != GuiCommandHandlerType::Deferred) return 18;
    gui["command"]["handler"] = "manual";
    if (!rejects()) return 19; // removed standalone manual handler must fail explicitly
    gui["command"]["handler"] = "tracking";
    if (config_parser::load_gui_config(gui).command.handler != GuiCommandHandlerType::Tracking) return 40;
    auto control = YAML::Load("enabled: true\nheartbeat_timeout_ms: 800\npoll_interval_ms: 20\ngimbal: {device: /dev/test, baud_rate: 57600, speed: 37, acceleration: 61, home_x_deg: 12, home_y_deg: -5, x_sign: -1, y_sign: 1, write_timeout_ms: 50}\n");
    auto control_config = config_parser::load_control_config(control);
    if (control_config.gimbal.device != "/dev/test" || control_config.gimbal.speed != 37 ||
        control_config.gimbal.acceleration != 61 || control_config.gimbal.home_x_deg != 12 ||
        control_config.gimbal.home_y_deg != -5 || control_config.gimbal.x_sign != -1 ||
        control_config.gimbal.baud_rate != 57600 || control_config.heartbeat_timeout_ms != 800) return 20;
    const auto invalid_control = [&] {
        try { config_parser::load_control_config(control); } catch (const std::runtime_error&) { return true; }
        return false;
    };
    control["gimbal"]["x_sign"] = 0;
    if (!invalid_control()) return 21;
    control["gimbal"]["x_sign"] = 1;
    control["gimbal"]["home_y_deg"] = 91;
    if (!invalid_control()) return 22;
    control["gimbal"]["home_y_deg"] = 0;
    control["gimbal"]["baud_rate"] = 12345;
    if (!invalid_control()) return 23;
    control["gimbal"]["baud_rate"] = 115200;
    control["poll_interval_ms"] = 900;
    if (!invalid_control()) return 24;
    // Enabling actual I/O must require the matching handler and TCP channel.
    char filename[] = "/tmp/lwir-control-config-XXXXXX";
    const int fd = ::mkstemp(filename);
    if (fd < 0) return 25;
    ::close(fd);
    struct RemoveConfig { const char* path; ~RemoveConfig() { ::unlink(path); } } cleanup{filename};
    auto runtime = config_parser::load_yaml("config/runtime.yaml");
    runtime["control"]["enabled"] = true;
    runtime["gui"]["enabled"] = true;
    runtime["gui"]["command"]["enabled"] = true;
    runtime["gui"]["command"]["handler"] = "tracking";
    const auto write_config = [&] { std::ofstream out(filename); out << runtime; };
    write_config();
    if (!load_runtime_config(filename).control.enabled) return 26;
    if (load_runtime_config(filename).gui.command.handler != GuiCommandHandlerType::Tracking) return 41;
    const auto invalid_combination = [&] {
        write_config();
        try { load_runtime_config(filename); } catch (const std::runtime_error&) { return true; }
        return false;
    };
    runtime["gui"]["command"]["handler"] = "deferred";
    if (!invalid_combination()) return 27;
    runtime["gui"]["command"]["handler"] = "tracking";
    runtime["gui"]["command"]["enabled"] = false;
    if (!invalid_combination()) return 28;
    runtime["gui"]["command"]["enabled"] = true;
    runtime["control"]["enabled"] = false;
    if (!invalid_combination()) return 29;
    // 상위 GUI/명령이 꺼지면 tracking 선택을 유지해도 실행 가능해야 한다.
    // 반대로 제어가 켜졌다면 GUI·명령·tracking 핸들러가 모두 필요하다.
    for (const auto* handler : {"deferred", "tracking"}) {
        for (unsigned flags = 0; flags < 8; ++flags) {
            runtime = YAML::Clone(yaml);
            const bool gui_on = flags & 1;
            const bool command_on = flags & 2;
            const bool control_on = flags & 4;
            const bool tracking = std::string(handler) == "tracking";
            runtime["gui"]["enabled"] = gui_on;
            runtime["gui"]["command"]["enabled"] = command_on;
            runtime["gui"]["command"]["handler"] = handler;
            runtime["gui"]["metadata"]["enabled"] = false;
            runtime["control"]["enabled"] = control_on;
            runtime["measurement"]["enabled"] = false;
            const bool expected = control_on ? (gui_on && command_on && tracking) :
                !(gui_on && command_on && tracking);
            if ((!invalid_combination()) != expected) return 51;
        }
    }
    runtime = YAML::Clone(yaml);
    runtime["control"]["enabled"] = false;
    runtime["gui"]["enabled"] = true;
    runtime["gui"]["command"]["handler"] = "tracking";
    // 비활성 명령의 tracking 선택값이 core/video-only 실행을 막지 않아야 한다.
    runtime["gui"]["command"]["enabled"] = false;
    write_config();
    if (load_runtime_config(filename).control.enabled) return 51;
    runtime["gui"]["enabled"] = false;
    runtime["gui"]["metadata"]["enabled"] = false;
    write_config();
    const auto core = load_runtime_config(filename);
    if (core.gui.enabled || core.control.enabled || core.gui.command.enabled ||
        core.gui.metadata.enabled || core.gui.command.handler != GuiCommandHandlerType::Tracking)
        return 52;
    // 상위 GUI가 꺼져 있으면 하위 command 플래그도 실행되지 않는다.
    runtime["gui"]["command"]["enabled"] = true;
    write_config();
    if (load_runtime_config(filename).gui.enabled) return 53;
    runtime = YAML::Clone(yaml);
    runtime["control"]["enabled"] = false;
    char model_filename[] = "/tmp/lwir-model-config-XXXXXX";
    const int model_fd = ::mkstemp(model_filename);
    if (model_fd < 0) return 42;
    ::close(model_fd);
    RemoveConfig model_cleanup{model_filename};
    auto alternate = config_parser::load_yaml("config/detection/yolo26s.yaml");
    alternate["camera_input"]["clip_max"] = 32000;
    { std::ofstream out(model_filename); out << alternate; }
    runtime["gui"]["command"]["handler"] = "deferred";
    runtime["detection_catalog"][1]["config"] = model_filename;
    if (!invalid_combination()) return 43;
    runtime = YAML::Clone(yaml);
    runtime["tracking"]["id"] = 99;
    if (!invalid_combination()) return 45;
    runtime = YAML::Clone(yaml);
    runtime["tracking_catalog"].push_back(runtime["tracking_catalog"][0]);
    if (!invalid_combination()) return 46;
    runtime = YAML::Clone(yaml);
    runtime["tracking"]["track_threshold"] = 0.9;
    if (!invalid_combination()) return 48;
    runtime = YAML::Clone(yaml);
    runtime["detection_catalog"][1]["id"] = 3;
    if (!invalid_combination()) return 49;
    runtime = YAML::Clone(yaml);
    runtime["tracking_catalog"][0]["id"] = 2;
    runtime["tracking"]["id"] = 2;
    if (!invalid_combination()) return 50;
    return 0;
}
