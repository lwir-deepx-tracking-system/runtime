#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
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

enum class NpuCoreBinding { All, Core0, Core1, Core2, Core01, Core12, Core02 };

struct InferenceConfig {
    std::size_t max_inflight = 1;
    int buffer_count = 0;
    bool use_ort = false;
    NpuCoreBinding npu_cores = NpuCoreBinding::All;
};

enum class TrackerType { ByteTrack };

struct TrackingConfig {
    TrackerType type = TrackerType::ByteTrack;
    float track_threshold = 0.5F;
    float match_threshold = 0.8F;
    int track_buffer = 30;

    // 아래 값은 runtime/benchmark yaml에서 생략할 수 있다. 생략하면 해당 기능은 꺼진다.

    // ByteTrack 세부 임계값. 생략하면 공식 ByteTrack 값을 쓴다.
    // 이 점수 이하인 검출은 버린다. low_threshold 초과 ~ track_threshold 미만은 2차 매칭에만 쓴다.
    float low_threshold = 0.1F;
    // 새 Track(ID)을 만드는 최소 점수. 0이면 track_threshold + 0.1 (최대 1)
    float new_track_threshold = 0.0F;
    // 2차 매칭(남은 Track ↔ low 검출) 허용 거리 (1 - IoU). 0.5면 IoU 0.5 이상
    float low_match_threshold = 0.5F;
    // 미확정 Track(직전 프레임에 생긴 Track) ↔ 남은 high 검출 매칭 허용 거리 (1 - IoU). 0.7이면 IoU 0.3 이상
    float unconfirmed_match_threshold = 0.7F;

    // 1차 매칭과 미확정 Track 매칭 비용에 검출 점수를 곱한다 (cost = 1 - IoU * score).
    bool fuse_score = false;
    // NSA Kalman (StrongSORT): Kalman update의 측정 노이즈를 (1 - 검출 점수)배 한다.
    // 점수가 높은 검출은 더 믿고, 낮은 검출(가려짐 등)은 Track 상태를 덜 흔든다.
    bool nsa_kalman = false;
    // Kalman 상태 형식. "xyah": [cx, cy, w/h, h] (공식 ByteTrack, 가로세로비는 거의 고정이라고 가정)
    //                   "xywh": [cx, cy, w, h] (BoT-SORT, 폭과 높이를 따로 추정해 폭 변화를 빨리 따라감)
    std::string kalman_format = "xyah";
    // 놓친 Track을 Kalman 예측 위치로 이 프레임 수만큼 더 출력한다 (0: 출력 안 함).
    int lost_output_frames = 0;
    // Buffered IoU: 1차 매칭 뒤 남은 Track과 high 검출을 박스를 넓혀 한 번 더 매칭한다.
    // 각 변을 폭·높이 × buffer_scale만큼 넓힌다 (0: 끔). 매칭 판단에만 쓰고 출력·Kalman에는 쓰지 않는다.
    float buffer_scale = 0.0F;
    // 한 변을 넓히는 최대 px (0: 제한 없음)
    float buffer_max_px = 0.0F;
    // Track 박스의 짧은 변이 이 px 이하일 때만 Buffered IoU 적용 (0: 크기 무관)
    float buffer_small_size = 0.0F;
    // Buffered IoU로만 맞은 Track의 위치를 검출 박스로 맞춘다 (프레임 누락에 유리, 흔들리는 작은 표적에 불리)
    bool buffer_snap = false;

    // ---- OC-SORT (Observation-Centric SORT, Cao et al., CVPR 2023) ----
    // 세 기능은 서로 독립이며 모두 기본 꺼짐이다. 과거 관측 박스는 CMC를 적용해 현재 화면 좌표로 유지한다.
    // ORU: Lost였던 Track을 다시 찾으면 놓치기 직전 Kalman 상태로 되돌리고, 마지막 관측 → 새 관측을
    //      직선 보간한 가상 관측으로 놓친 구간을 다시 predict/update 한다 (Lost 동안 틀어진 속도를 바로잡음).
    //      다시 찾은 Track에는 buffer_snap을 적용하지 않는다.
    bool oru = false;
    // OCM: 1차 매칭 비용에서 이동 방향 일치도 × 검출 점수 × 이 가중치를 뺀다 (0: 끔, OC-SORT 기본 0.2).
    //      매칭 허용 여부는 지금처럼 IoU 비용(match_threshold)으로만 판단하고, 방향 항은 후보 사이 순위에만 쓴다.
    float ocm_weight = 0.0F;
    // OCM 방향을 계산할 관측 간격(프레임). OC-SORT 기본 3
    int ocm_delta_t = 3;
    // OCR: 2차 매칭 뒤 남은 Track을 Kalman 예측 대신 마지막 관측 박스로 남은 high 검출과 한 번 더 매칭한다
    //      (match_threshold 사용). 멈춘 물체·짧은 가림 뒤 예측이 빗나간 Track을 복구한다.
    bool ocr = false;
};

struct GimbalConfig {
    std::string device = "/dev/ttyUSB0";
    int baud_rate = 115200;
    int write_timeout_ms = 100;
    int speed = 50; // manual/HOME command speed
    int speed_limit = 100; // shared software command ceiling, independent of manual speed
    int acceleration = 50;
    int home_x_deg = 0;
    int home_y_deg = 0;
    int x_sign = 1;
    int y_sign = 1;
};

enum class AutoControllerType { P, PI, PD, PID };

struct PidAxisConfig {
    double kp = 30.0;
    double ki = 0.0;
    double kd = 0.0;
    double integral_limit = 10.0; // absolute I contribution, in SPD command units
    double derivative_filter_ms = 100.0;
};

// SPD values are vendor command units, not degrees/second.
struct AutoControlConfig {
    AutoControllerType controller = AutoControllerType::P;
    PidAxisConfig pid_x;
    PidAxisConfig pid_y;
    double deadband_x = 0.04; // normalized half-frame error, or radians with FOV
    double deadband_y = 0.04;
    double fov_x_deg = 0.0; // both zero selects normalized pixel errors
    double fov_y_deg = 0.0;
    double min_confidence = 0.5;
    int speed_min = 1;
    int speed_max = 100;
    double speed_rate = 60.0; // SPD units/second; safety STOP bypasses this
    int period_ms = 25;
    int input_timeout_ms = 250;
    int reacquire_timeout_ms = 1000;
};

struct ControlConfig {
    bool enabled = false;
    int heartbeat_timeout_ms = 1500;
    int poll_interval_ms = 25;
    GimbalConfig gimbal;
    AutoControlConfig automatic;
};

struct MeasurementConfig {
    bool enabled = false;
    std::string output_root;
};

// runtime.yaml의 gui 설정은 세 개의 독립적인 통신 경로를 정의한다.
// Video    : Orange Pi -> PC GUI, JPEG or H.264/RTP/UDP
// Metadata : Orange Pi -> PC GUI, frame/Track 정보 UDP
// Command  : PC 명령 및 보드 ACK/상태 응답 TCP
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

enum class GuiCommandHandlerType { Deferred, Tracking };

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
    GuiCommandHandlerType handler = GuiCommandHandlerType::Deferred;
    // PC 명령 및 상태 응답을 전달하는 TCP listener 조건이다.
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

// ReID 모델과 크롭·embedding 출력 계약. 모델별 YAML에서 검증된 값만 전달한다.
struct ReidModelConfig {
    std::string name;
    std::string path;
    int input_width = 0;
    int input_height = 0;
    int input_channels = 0;
    std::string input_dtype;
    std::string input_layout;
    std::string resize;
    std::string channel_mode;
    std::string output_name;
    int output_dimensions = 0;
    std::string output_dtype;
    std::string output_normalize;
};

struct ReidBenchmarkConfig {
    int person_class_id = 0;
    int min_crop_height = 0;
    int max_crops_per_frame = 0;
};

struct ReidMatcherConfig {
    int person_class_id = 0;
    int max_lost_frames = 90;
    int gallery_size = 5;
    int decision_wait_frames = 3;
    float similarity_threshold = 0.596F;
    float min_height_ratio = 0.5F;
    float max_height_ratio = 2.0F;
    float max_center_distance_height = 3.5F;
    float distance_growth_per_10_frames = 1.0F;
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


// 모델 ID에 모델 YAML 경로와 파싱된 설정을 연결한다.
struct ModelCatalogEntry
{
    int id = 0;
    std::string config_path;
    ModelConfig model;
};

struct DetectionSettings
{
    std::string config_path;
    ModelConfig model;
    InferenceConfig inference;
};

struct ReidSettings
{
    std::string config_path;
    ReidModelConfig model;
    InferenceConfig inference;
    std::string matcher_config_path;
    ReidMatcherConfig matcher;
};

// 트래커 ID에 설정 YAML 경로와 검증된 typed 설정을 연결한다.
struct TrackingCatalogEntry
{
    int id = 0;
    std::string config_path;
    TrackingConfig config;
};

// runtime YAML과 참조된 모델·트래커 YAML을 합친 실행 설정이다.
struct RuntimeConfig
{
    LoggingConfig logging;
    DetectionSettings detection;
    int initial_detection_id = 0;
    std::map<int, ModelCatalogEntry> detection_catalog;
    std::map<int, TrackingCatalogEntry> tracking_catalog;
    int initial_tracker_id = 0;
    ReidSettings reid;
    TrackingConfig tracking;
    ControlConfig control;
    MeasurementConfig measurement;
    GuiConfig gui;
};

RuntimeConfig load_runtime_config(const std::string& path);

ModelConfig load_model_config(const std::string& config_path);
ReidModelConfig load_reid_config(const std::string& config_path);
ReidMatcherConfig load_reid_matcher_config(const std::string& config_path);
// 트래커 전용 YAML을 읽고 종류별 설정을 검증한다.
TrackingConfig load_tracker_config(const std::string& config_path);

namespace config_parser
{
YAML::Node load_yaml(const std::string& path);
std::string required_string(
    const YAML::Node& node,
    const char* key,
    const std::string& path);

LoggingConfig load_logging_config(const YAML::Node& node);
InferenceConfig load_inference_config(const YAML::Node& node, const std::string& path);
TrackingConfig load_tracking_config(const YAML::Node& node);
ReidBenchmarkConfig load_reid_benchmark_config(const YAML::Node& node);
// 트래커 카탈로그의 ID·경로를 검증하고 각 YAML을 typed 설정으로 읽는다.
std::map<int, TrackingCatalogEntry> load_tracking_catalog(const YAML::Node& node);
// 실행 YAML의 tracking.id가 카탈로그에 존재하는지 확인한다.
int select_tracking_id(
    const YAML::Node& node, const std::map<int, TrackingCatalogEntry>& catalog);
// 실행 YAML의 tracking.id에 해당하는 검증된 추적 설정을 선택한다.
TrackingConfig select_tracking_config(
    const YAML::Node& node, const std::map<int, TrackingCatalogEntry>& catalog);
ControlConfig load_control_config(const YAML::Node& node);
MeasurementConfig load_measurement_config(const YAML::Node& node);
GuiConfig load_gui_config(const YAML::Node& node);
BenchmarkPathConfig load_benchmark_path_config(const YAML::Node& node);
BenchmarkSequenceConfig load_benchmark_sequence_config(const YAML::Node& node);
CaptureDatasetConfig load_capture_dataset_config(const YAML::Node& node);
CaptureSequenceConfig load_capture_sequence_config(const YAML::Node& node);
CaptureConditionConfig load_capture_condition_config(const YAML::Node& node);
}
