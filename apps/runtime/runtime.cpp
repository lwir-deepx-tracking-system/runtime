#include "runtime.hpp"
#include "common/logger.hpp"
#include "common/frame.hpp"
#include "common/threadsafequeue.hpp"
#include "common/detection.hpp"
#include "common/track.hpp"

#include "detection/dxapp_detection_pipeline.hpp"
#include "tracking/bytetrack_tracker.hpp"

#include "camera/camera.hpp"
#include "gui/gui_receiver.hpp"
#include "gui/gui_sender.hpp"
#include "target/target_selector.hpp"
#include "tracking/tracker.hpp"

#include "pipeline/camera_thread.hpp"
#include "pipeline/detection_thread.hpp"
#include "pipeline/tracking_thread.hpp"
#include "pipeline/control_thread.hpp"
#include "pipeline/gui_receiver_thread.hpp"
#include "pipeline/gui_sender_thread.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>


namespace fs = std::filesystem;

// runtime 설정을 읽고 별도 모델 YAML까지 한 번에 준비한다.
RuntimeConfig load_runtime_config(const std::string& path)
{
    const YAML::Node root = config_parser::load_yaml(path);
    RuntimeConfig c;
    c.logging = config_parser::load_logging_config(root["logging"]);
    Logger::set_level(c.logging.level);

    c.model_config_path = config_parser::required_string(
        root["model"], "config", "model");
    if (c.model_config_path.empty())
        throw std::runtime_error("model.config must not be empty");
    c.model = load_model_config(c.model_config_path);

    c.detection = config_parser::load_detection_config(root["detection"]);
    c.tracking = config_parser::load_tracking_config(root["tracking"]);
    c.control = config_parser::load_control_config(root["control"]);
    c.measurement = config_parser::load_measurement_config(root["measurement"]);
    c.gui = config_parser::load_gui_config(root["gui"]);
    Logger::info("[Config] runtime and model configuration loaded");
    return c;
}

// 종료된 worker의 측정값을 한 CSV에 모은다.
static void append_metrics(
    std::ofstream& file,
    const char* stage,
    const std::vector<StageMetric>& metrics)
{
    for (const StageMetric& metric : metrics)
    {
        file << "integrated," << metric.frame_id << ',' << stage << ',';
        if (metric.queue_wait_ms)
            file << *metric.queue_wait_ms;
        file << ',' << metric.processing_ms << ',';
        if (metric.e2e_ms)
            file << *metric.e2e_ms;
        file << '\n';
    }
}


// 프로그램의 구성 요소를 생성하고
// Pipeline 실행을 관리하는 최상위 Application 클래스
class RuntimeApplication
{
private:
    // Camera의 shared FrameContext를 Detection으로 전달
    ThreadSafeQueue<FrameMessage> frame_queue_;

    // Detection 목록 다음 Stage로 전달
    ThreadSafeQueue<DetectionResult> detection_queue_;

    // 같은 TrackingResult를 제어와 GUI 경로가 공유한다.
    ThreadSafeQueue<TrackingResultPtr> control_track_queue_;
    ThreadSafeQueue<TrackingResultPtr> gui_track_queue_;

    std::string config_path_;
    std::vector<std::string> config_snapshot_paths_;
    bool measurement_enabled_ = false;

/************************************************************************/
    // Camera 객체
    std::unique_ptr<Camera> camera_;

    // Camera pthread
    std::unique_ptr<CameraThread> camera_thread_;

    // DX 전처리, 추론, 후처리를 소유하는 통합 Detection 단계
    std::unique_ptr<DetectionPipeline> detection_pipeline_;
    std::unique_ptr<DetectionThread> detection_thread_;

    // 현재 사용하는 Tracker 객체를 Application이 소유한다.
    std::unique_ptr<Tracker> tracker_;

    // Tracking pthread
    std::unique_ptr<TrackingThread> tracking_thread_;

    TargetSelector target_selector_;

    GimbalController gimbal_controller_;
    std::unique_ptr<ControlThread> control_thread_;

    // Application이 GUI 통신 객체의 전체 수명을 소유한다. Sender 내부의
    // GStreamer pipeline과 Receiver 내부의 TCP socket은 각 객체가 RAII로
    // 정리하고, worker는 실행 thread만 담당한다.
    std::unique_ptr<GuiSender> gui_sender_;
    std::unique_ptr<GuiSenderThread> gui_sender_thread_;
    std::unique_ptr<GuiReceiver> gui_receiver_;
    std::unique_ptr<GuiReceiverThread> gui_receiver_thread_;


public:
    // YAML 설정을 읽고 필요한 객체를 생성
    explicit RuntimeApplication(const std::string& config_path);

    // Pipeline Worker 실행
    void run();
};


// YAML을 읽고 설정에 맞는 Component들을 생성한다.
RuntimeApplication::RuntimeApplication(const std::string& config_path)
    : config_path_(config_path)
{
    // YAML 로그 레벨을 먼저 적용한 뒤 초기 큐 상태를 출력한다.
    RuntimeConfig config = load_runtime_config(config_path);
    measurement_enabled_ = config.measurement.enabled;

    Logger::debug("[RuntimeApplication] 큐 생성 완료");

    Logger::debug(
        "[RuntimeApplication] frame_queue 크기: " +
        std::to_string(frame_queue_.size())
    );

    Logger::debug(
        "[RuntimeApplication] detection_queue 크기: " +
        std::to_string(detection_queue_.size())
    );

    Logger::debug(
        "[RuntimeApplication] control_track_queue 크기: " +
        std::to_string(control_track_queue_.size())
    );

    Logger::debug(
        "[RuntimeApplication] gui_track_queue 크기: " +
        std::to_string(gui_track_queue_.size())
    );

    // 결과 폴더에는 실행 당시 참조한 YAML을 사본으로 남긴다.
    config_snapshot_paths_ = {
        config_path_,
        config.model_config_path
    };

    camera_ = std::make_unique<Camera>();

    // 이 런타임의 고정 조합인 YOLOv8 + DX App pipeline을 생성한다.
    detection_pipeline_ = std::make_unique<DxAppDetectionPipeline>(
        config.model, config.detection);

    // AppConfig가 검증한 runtime 설정으로 고정 ByteTrack 구현체를 생성한다.
    tracker_ = std::make_unique<ByteTrackTracker>(config.tracking);

    
    // Camera Thread에 연결
    // Camera → frame_queue
    camera_thread_ =
        std::make_unique<CameraThread>(
            *camera_,
            frame_queue_,
            measurement_enabled_
        );

    detection_thread_ =
        std::make_unique<DetectionThread>(
            *detection_pipeline_,
            frame_queue_,
            detection_queue_,
            measurement_enabled_
        );
    
    // detection_queue → Tracking → track_queue
    tracking_thread_ =
        std::make_unique<TrackingThread>(
            *tracker_,
            detection_queue_,
            control_track_queue_,
            gui_track_queue_,
            measurement_enabled_
        );

    // Tracking 결과와 GUI 선택 ID를 Orange Pi 짐벌 제어 단계에 직접 연결한다.
    control_thread_ =
        std::make_unique<ControlThread>(
            gimbal_controller_,
            target_selector_,
            control_track_queue_,
            config.control.enabled,
            measurement_enabled_
        );

    if (config.gui.enabled)
    {
        gui_sender_ = std::make_unique<GuiSender>(
            config.gui.video,
            config.gui.metadata,
            config.model.camera_input.clip_min,
            config.model.camera_input.clip_max);
        gui_sender_thread_ = std::make_unique<GuiSenderThread>(
            *gui_sender_, gui_track_queue_);

        if (config.gui.command.enabled)
        {
            gui_receiver_ = std::make_unique<GuiReceiver>(config.gui.command);
            gui_receiver_thread_ = std::make_unique<GuiReceiverThread>(
                *gui_receiver_, target_selector_);
        }
    }
}

// Pipeline Worker를 실행한다.
void RuntimeApplication::run()
{
    // 수신기는 pipeline보다 먼저 열어 GUI가 언제든 선택 명령을 보낼 수 있게
    // 하고, 송신기는 Tracking 결과 queue를 기다리도록 먼저 시작한다.
    if (gui_receiver_thread_) gui_receiver_thread_->start();
    if (gui_sender_thread_) gui_sender_thread_->start();

    control_thread_->start();
    tracking_thread_->start();
    detection_thread_->start();
    // 마지막에 Frame 생산자 시작
    camera_thread_->start();


    // 각 Thread 종료 대기
    camera_thread_->join();
    detection_thread_->join();
    tracking_thread_->join();
    if (gui_sender_thread_) gui_sender_thread_->join();
    control_thread_->join();

    // TCP receiver는 외부 GUI 연결을 기다릴 수 있으므로 명시적으로 stop해
    // poll/recv를 깨운 뒤 join한다.
    if (gui_receiver_thread_)
    {
        gui_receiver_thread_->stop();
        gui_receiver_thread_->join();
    }

    if (!measurement_enabled_)
        return;

    // 측정 모드에서만 실행 후 CSV와 YAML 사본을 저장한다.
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_r(&now_time, &local_time);

    const std::string model_name = fs::path(config_snapshot_paths_[1]).stem().string();
    std::ostringstream name;
    name << model_name << '_' << std::put_time(&local_time, "%Y%m%d_%H%M%S");
    const std::string base_name = name.str();

    fs::create_directories("results");
    fs::path result_dir;
    for (std::size_t suffix = 0;; ++suffix)
    {
        result_dir = fs::path("results") /
            (base_name + (suffix == 0 ? "" : "_" + std::to_string(suffix)));
        if (fs::create_directory(result_dir))
            break;
    }

    const fs::path snapshot_dir = result_dir / "config";
    fs::create_directory(snapshot_dir);
    for (const std::string& path : config_snapshot_paths_)
        fs::copy_file(path, snapshot_dir / fs::path(path).filename());

    std::ofstream metrics_file(result_dir / "metrics.csv");
    if (!metrics_file)
        throw std::runtime_error("metrics.csv 파일을 생성하지 못했습니다");
    metrics_file << "mode,frame_id,stage,queue_wait_ms,processing_ms,e2e_ms\n";
    metrics_file << std::fixed << std::setprecision(6);
    append_metrics(metrics_file, "camera", camera_thread_->metrics());
    append_metrics(metrics_file, "detection", detection_thread_->metrics());
    append_metrics(metrics_file, "tracking", tracking_thread_->metrics());
    append_metrics(metrics_file, "control", control_thread_->metrics());
    metrics_file.close();
    if (!metrics_file)
        throw std::runtime_error("metrics.csv 파일을 저장하지 못했습니다");

    Logger::info("[Measurement] 측정 결과 저장 완료: " + result_dir.string());
}

int run_runtime(const std::string& config_path)
{
    RuntimeApplication app(config_path);
    app.run();
    return 0;
}
