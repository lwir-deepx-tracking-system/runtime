#include "app/app_config.hpp"

// 공용 YAML이 runtime에서 지원하는 Detection 및 LWIR 계약으로 파싱되는지 확인한다.
int main()
{
    const AppConfig config = load_config("config/runtime.yaml");

    // runtime.yaml의 모델 경로와 고정 Detection 실행 설정 검증.
    if (config.model_config_path != "config/model/yolov8n.yaml" ||
        config.detection.max_inflight != 4) return 1;

    // ByteTrack, Control, Measurement 설정도 runtime.yaml에서 직접 읽는다.
    if (config.tracking.track_threshold != 0.5F ||
        config.tracking.match_threshold != 0.8F ||
        config.tracking.track_buffer != 30 ||
        config.control.enabled ||
        config.control.driver != "unconfigured" ||
        config.measurement.enabled)
        return 2;

    // 연결된 모델 YAML의 Camera와 모델 입력 계약 검증.
    if (config.model.camera_input.opencv_type != "CV_16UC1") return 3;
    if (config.model.camera_input.width != 640 || config.model.camera_input.height != 480) return 4;
    if (config.model.input.width != 640 || config.model.input.height != 640) return 5;

    // 전처리 padding과 class 개수가 후처리 설정까지 전달되는지 확인한다.
    if (config.model.preprocess.pad_value != 114 || config.model.postprocess.num_classes != 1) return 6;

    // GUI 설정도 runtime.yaml에서 AppConfig를 통해 typed 값으로 전달된다.
    if (config.gui.enabled ||
        config.gui.video.host != "127.0.0.1" ||
        config.gui.video.port != 5000 ||
        config.gui.video.codec != GuiVideoCodec::H264 ||
        config.gui.video.encoder != "x264enc" ||
        config.gui.video.bitrate_kbps != 3000 ||
        config.gui.video.fps != 30 ||
        config.gui.video.rtp_mtu != 1200 ||
        !config.gui.metadata.enabled ||
        config.gui.metadata.host != "127.0.0.1" ||
        config.gui.metadata.port != 5002 ||
        config.gui.metadata.max_packet_bytes != 1200 ||
        !config.gui.command.enabled ||
        config.gui.command.bind_address != "0.0.0.0" ||
        config.gui.command.port != 5001 ||
        config.gui.command.receive_timeout_ms != 1000)
        return 7;
    return 0;
}
