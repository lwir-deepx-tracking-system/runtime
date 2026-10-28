#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace YAML
{
class Node;
}

// runtime.yaml에서 읽는 실행 단계별 설정.
struct LoggingConfig {
    std::string level;
};

struct DetectionConfig {
    std::size_t max_inflight = 1;
    int buffer_count = 0;
    bool use_ort = false;
};

struct TrackingConfig {
    float track_threshold = 0.5F;
    float match_threshold = 0.8F;
    int track_buffer = 30;
};

struct ControlConfig {
    bool enabled = false;
};

struct MeasurementConfig {
    bool enabled = false;
    std::string output_root;
};

// runtime.yaml의 gui 설정은 세 개의 독립적인 통신 경로를 정의한다.
// Video    : Orange Pi -> PC GUI, JPEG or H.264/RTP/UDP
// Metadata : Orange Pi -> PC GUI, frame/Track 정보 UDP
// Command  : PC GUI -> Orange Pi, 선택 track_id TCP
enum class GuiVideoCodec
{
    H264,
    JPEG
};

struct GuiVideoConfig
{
    // JPEG or H.264/RTP 영상 목적지와 GStreamer encode 조건이다.
    std::string host;
    std::uint16_t port = 0;
    GuiVideoCodec codec = GuiVideoCodec::H264;
    std::string encoder;
    int bitrate_kbps = 0;
    int jpeg_quality = 80;
    int keyframe_interval = 30;
    int fps = 0;
    int rtp_mtu = 0;
};

struct GuiMetadataConfig
{
    // 영상과 별도 포트로 보내는 Tracking metadata UDP 조건이다.
    bool enabled = false;
    std::string host;
    std::uint16_t port = 0;
    std::size_t max_packet_bytes = 0;
};

struct GuiCommandConfig
{
    // GUI 선택 명령을 기다릴 TCP listener 조건이다.
    bool enabled = false;
    std::string bind_address;
    std::uint16_t port = 0;
    int receive_timeout_ms = 0;
};

struct GuiConfig
{
    // enabled가 false이면 Application은 GUI sender/receiver worker를 만들지 않는다.
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
    std::string type;
    std::string resize;
    bool normalize = false;
    int pad_value = 114;
};

// DXNN/ORT가 노출하는 detection output tensor의 계약.
enum class PostprocessOutputFormat
{
    Auto,
    DecodedXywh,
    EndToEndXyxy,
    SplitHead
};

// split-head class tensor가 logit인지 이미 sigmoid가 적용된 확률인지 구분한다.
enum class PostprocessScoreActivation
{
    None,
    Sigmoid
};

// End-to-end output은 기본적으로 NMS를 생략하고 raw output에는 적용할 수 있다.
enum class PostprocessNmsMode
{
    Auto,
    Enabled,
    Disabled
};

// 모델 output decode, 결과 필터링과 class label 변환에 사용하는 값.
struct ModelPostprocessConfig {
    std::string type;
    PostprocessOutputFormat output_format = PostprocessOutputFormat::Auto;
    int split_output_levels = 3;
    int split_regression_bins = 16;
    PostprocessScoreActivation split_class_score_activation =
        PostprocessScoreActivation::None;
    PostprocessNmsMode nms_mode = PostprocessNmsMode::Auto;
    bool class_agnostic_nms = true;
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

/********** Benchmark 어플리케이션에서 사용하는 데이터셋 경로와 출력 경로 설정  ***********/

struct BenchmarkPathConfig
{
    std::string dataset_root;
    std::string output_root;
};

// Benchmark가 처리할 Sequence ID의 양끝 포함 범위.
struct BenchmarkSequenceConfig
{
    bool range_enabled = false;
    std::string start;
    std::string end;

    bool contains(const std::string& sequence_id) const;
};


/********** Capture 어플리케이션에서 사용하는 설정 ***********/

// 촬영 데이터가 저장될 최상위 경로
struct CaptureDatasetConfig
{
    std::string root;
};

// 이번 촬영 Sequence 식별자
struct CaptureSequenceConfig
{
    std::string id;
};

// 이번 촬영의 실험 조건
struct CaptureConditionConfig
{
    float distance_m = 0.0F;
    int person_count = 0;
    std::string motion;
    float temperature_c = 0.0F;
};


ModelConfig load_model_config(const std::string& config_path);

namespace config_parser
{
YAML::Node load_yaml(const std::string& path);
std::string required_string(
    const YAML::Node& node,
    const char* key,
    const std::string& path);

LoggingConfig load_logging_config(const YAML::Node& node);
DetectionConfig load_detection_config(const YAML::Node& node);
TrackingConfig load_tracking_config(const YAML::Node& node);
ControlConfig load_control_config(const YAML::Node& node);
MeasurementConfig load_measurement_config(const YAML::Node& node);
GuiConfig load_gui_config(const YAML::Node& node);
BenchmarkPathConfig load_benchmark_path_config(const YAML::Node& node);
BenchmarkSequenceConfig load_benchmark_sequence_config(const YAML::Node& node);
CaptureDatasetConfig load_capture_dataset_config(const YAML::Node& node);
CaptureSequenceConfig load_capture_sequence_config(const YAML::Node& node);
CaptureConditionConfig load_capture_condition_config(const YAML::Node& node);
}
