#include "runtime/runtime.hpp"
#include <yaml-cpp/yaml.h>
#include <stdexcept>

// 공용 YAML이 runtime에서 지원하는 Detection 및 LWIR 계약으로 파싱되는지 확인한다.
int main()
{
    const auto yaml = YAML::LoadFile("config/runtime.yaml");
    const RuntimeConfig config = load_runtime_config("config/runtime.yaml");

    // runtime.yaml의 모델 경로와 고정 Detection 실행 설정 검증.
    if (config.model_config_path != "config/model/yolov8n.yaml" ||
        config.detection.max_inflight != 4 ||
        config.detection.buffer_count != 6 ||
        config.detection.use_ort) return 1;

    // ByteTrack, Control, Measurement 설정도 runtime.yaml에서 직접 읽는다.
    if (config.tracking.track_threshold != 0.5F ||
        config.tracking.match_threshold != 0.8F ||
        config.tracking.track_buffer != 30 ||
        config.control.enabled ||
        !config.measurement.enabled ||
        config.measurement.output_root != "/mnt/lwir_data/results/runtime")
        return 2;

    // 연결된 모델 YAML의 Camera와 모델 입력 계약 검증.
    if (config.model.camera_input.opencv_type != "CV_16UC1") return 3;
    if (config.model.camera_input.width != 640 || config.model.camera_input.height != 480) return 4;
    if (config.model.input.width != 640 || config.model.input.height != 640) return 5;

    // 모델 YAML의 전처리와 output tensor 계약이 typed 설정으로 전달되는지 확인한다.
    if (config.model.preprocess.pad_value != 114 ||
        config.model.postprocess.output_format != PostprocessOutputFormat::Auto ||
        config.model.postprocess.split_output_levels != 3 ||
        config.model.postprocess.split_regression_bins != 16 ||
        config.model.postprocess.split_class_score_activation !=
            PostprocessScoreActivation::Sigmoid ||
        config.model.postprocess.nms_mode != PostprocessNmsMode::Enabled ||
        !config.model.postprocess.class_agnostic_nms ||
        config.model.postprocess.num_classes != 80)
        return 6;

    // YOLO26은 같은 parser를 사용하되 DFL-free split-head와 NMS auto 계약을 갖는다.
    const ModelConfig yolo26 = load_model_config("config/model/yolo26s.yaml");
    if (yolo26.name != "yolo26s" ||
        yolo26.postprocess.output_format != PostprocessOutputFormat::Auto ||
        yolo26.postprocess.split_output_levels != 3 ||
        yolo26.postprocess.split_regression_bins != 1 ||
        yolo26.postprocess.split_class_score_activation !=
            PostprocessScoreActivation::Sigmoid ||
        yolo26.postprocess.nms_mode != PostprocessNmsMode::Auto ||
        !yolo26.postprocess.class_agnostic_nms ||
        yolo26.postprocess.num_classes != 16)
        return 7;

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
    return 0;
}
