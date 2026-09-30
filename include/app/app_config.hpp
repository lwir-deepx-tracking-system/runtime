#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// runtime.yaml에서 읽는 실행 단계별 설정.
struct LoggingConfig {
    std::string level;
};

struct DetectionConfig {
    std::size_t max_inflight = 1;
};

struct TrackingConfig {
    float track_threshold = 0.5F;
    float match_threshold = 0.8F;
    int track_buffer = 30;
};

struct ControlConfig {
    bool enabled = false;
    std::string driver;
};

struct MeasurementConfig {
    bool enabled = false;
};

// GUI에 표시할 영상은 H.264로 인코딩한 뒤 RTP/UDP로 보낸다.
enum class GuiVideoCodec
{
    H264
};

struct GuiVideoConfig
{
    std::string host;
    std::uint16_t port = 0;
    GuiVideoCodec codec = GuiVideoCodec::H264;
    std::string encoder;
    int bitrate_kbps = 0;
    int fps = 0;
    int rtp_mtu = 0;
};

struct GuiMetadataConfig
{
    bool enabled = false;
    std::string host;
    std::uint16_t port = 0;
    std::size_t max_packet_bytes = 0;
};

struct GuiCommandConfig
{
    bool enabled = false;
    std::string bind_address;
    std::uint16_t port = 0;
    int receive_timeout_ms = 0;
};

struct GuiConfig
{
    bool enabled = false;
    GuiVideoConfig video;
    GuiMetadataConfig metadata;
    GuiCommandConfig command;
};

// 모델 YAML이 요구하는 원본 LWIR 영상의 형식과 유효 온도값 범위.
struct CameraInputConfig {
    std::string opencv_type;
    int width = 0;
    int height = 0;
    std::uint16_t clip_min = 0;
    std::uint16_t clip_max = 0;
    std::string channel_mode;
};

// DEEPX 모델 tensor의 가로·세로 크기.
struct ModelInputConfig {
    int width = 0;
    int height = 0;
};

// 전처리 방식은 학습·모델 변환 시 사용한 조건과 일치해야 한다.
struct ModelPreprocessConfig {
    std::string resize;
    bool normalize = false;
    int pad_value = 114;
};

// YOLOv8 결과 필터링과 class label 변환에 사용하는 값.
struct ModelPostprocessConfig {
    float confidence_threshold = 0.25F;
    float nms_threshold = 0.45F;
    int num_classes = 0;
    std::vector<std::string> class_names;
};

// 하나의 모델 YAML을 파싱한 불변 실행 계약.
struct ModelConfig {
    std::string name;
    std::string path;
    CameraInputConfig camera_input;
    ModelInputConfig input;
    ModelPreprocessConfig preprocess;
    ModelPostprocessConfig postprocess;
};

// runtime.yaml과 연결된 모델 YAML을 합친 최상위 설정.
struct AppConfig {
    LoggingConfig logging;
    std::string model_config_path;
    ModelConfig model;
    DetectionConfig detection;
    TrackingConfig tracking;
    ControlConfig control;
    MeasurementConfig measurement;
    GuiConfig gui;
};

// runtime 설정을 읽고 참조한 모델 설정까지 함께 검증한다.
AppConfig load_config(const std::string& config_path);

// 모델별 입출력·전후처리 설정을 읽고 지원 범위를 검증한다.
ModelConfig load_model_config(const std::string& config_path);
