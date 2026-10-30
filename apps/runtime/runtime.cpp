#include "runtime.hpp"
#include "common/logger.hpp"
#include "common/frame.hpp"
#include "common/threadsafequeue.hpp"
#include "common/detection.hpp"
#include "common/track.hpp"

#include "detection/detector_factory.hpp"
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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace {
volatile std::sig_atomic_t g_stop_requested = 0;
constexpr const char* kTrackerName = "bytetrack";

enum class RuntimeState
{
    Running,
    Switching,
    SwitchFailed
};

struct ModelChangeRequest
{
    std::uint64_t request_id = 0;
    std::string config_path;
    ModelConfig model;
};

void handle_sigint(int)
{
    g_stop_requested = 1;
}

// model/tracker와 자동 판별한 실행 모드 아래에 충돌 없는 timestamp를 만든다.
fs::path create_result_directory(
    const fs::path& output_root,
    const std::string& model_name,
    const std::string& runtime_mode)
{
    const fs::path mode_dir = output_root /
        (model_name + "_" + kTrackerName) / runtime_mode;
    fs::create_directories(mode_dir);

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    if (localtime_r(&now_time, &local_time) == nullptr)
        throw std::runtime_error("Runtime timestamp 생성에 실패했습니다");

    std::ostringstream stream;
    stream << std::put_time(&local_time, "%Y%m%d_%H%M%S");
    if (!stream)
        throw std::runtime_error("Runtime timestamp 생성에 실패했습니다");
    const std::string timestamp = stream.str();

    // 같은 초에 시작한 실행은 기존 결과를 보존하며 suffix를 증가시킨다.
    for (std::size_t suffix = 0;; ++suffix)
    {
        const fs::path result_dir = mode_dir /
            (timestamp + (suffix == 0 ? "" : "_" + std::to_string(suffix)));
        if (fs::create_directory(result_dir))
            return result_dir;
    }
}
}  // namespace

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

    if (!c.gui.enabled && c.control.enabled)
        throw std::runtime_error(
            "유효하지 않은 Runtime 설정입니다: control.enabled=true에는 "
            "gui.enabled=true가 필요합니다");

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
        file << metric.frame_id << ',' << stage << ',';
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
    RuntimeConfig config_;

    // Camera의 shared FrameContext를 Detection으로 전달
    ThreadSafeQueue<FrameMessage> frame_queue_;

    // Detection 목록 다음 Stage로 전달
    ThreadSafeQueue<DetectionResult> detection_queue_;

    // 같은 TrackingResult를 제어와 GUI 경로가 공유한다.
    ThreadSafeQueue<TrackingResultPtr> control_track_queue_;
    ThreadSafeQueue<TrackingResultPtr> gui_track_queue_;

    std::string config_path_;
    std::string runtime_mode_;
    bool measurement_enabled_ = false;
    std::atomic<RuntimeState> runtime_state_{RuntimeState::Running};
    std::mutex model_change_mutex_;
    std::uint64_t next_model_request_id_ = 1;
    ThreadSafeQueue<ModelChangeRequest> model_change_requests_;

/************************************************************************/
    // Camera 객체
    std::unique_ptr<Camera> camera_;

    // Camera pthread
    std::unique_ptr<CameraThread> camera_thread_;

    // DX 전처리, 추론, 후처리를 소유하는 통합 Detection 단계
    std::unique_ptr<IDetectionPipeline> detection_pipeline_;
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
    bool request_model_change(const std::string& model_config_path);
    RuntimeState runtime_state() const { return runtime_state_.load(); }
    void run();
};


// YAML을 읽고 설정에 맞는 Component들을 생성한다.
RuntimeApplication::RuntimeApplication(const std::string& config_path)
    : config_(load_runtime_config(config_path)), config_path_(config_path)
{
    measurement_enabled_ = config_.measurement.enabled;

    // Control은 GUI 선택 ID에 의존하므로 enable 조합만으로 세 모드를 결정한다.
    if (config_.control.enabled)
        runtime_mode_ = "full";
    else if (config_.gui.enabled)
        runtime_mode_ = "gui";
    else
        runtime_mode_ = "core";
    Logger::info("[Runtime] 실행 모드: " + runtime_mode_);

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

    camera_ = std::make_unique<Camera>();

    // model YAML의 전처리·후처리 계약으로 DXRT detection pipeline을 생성한다.
    auto detector_factory = DetectorFactory::for_model(config_.model);
    detection_pipeline_ = detector_factory->create(
        config_.model, config_.detection);

    // AppConfig가 검증한 runtime 설정으로 고정 ByteTrack 구현체를 생성한다.
    tracker_ = std::make_unique<ByteTrackTracker>(config_.tracking);

    
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

    /// TODO: add control enabled

    // Tracking 결과와 GUI 선택 ID를 Orange Pi 짐벌 제어 단계에 직접 연결한다.
    control_thread_ =
        std::make_unique<ControlThread>(
            gimbal_controller_,
            target_selector_,
            control_track_queue_,
            config_.control.enabled,
            measurement_enabled_
        );


    if (config_.gui.enabled)
    {
        gui_sender_ = std::make_unique<GuiSender>(
            config_.gui.video,
            config_.gui.metadata,
            config_.model.camera_input.clip_min,
            config_.model.camera_input.clip_max);
        gui_sender_thread_ = std::make_unique<GuiSenderThread>(
            *gui_sender_, gui_track_queue_);

        if (config_.gui.command.enabled)
        {
            gui_receiver_ = std::make_unique<GuiReceiver>(config_.gui.command);
            gui_receiver_thread_ = std::make_unique<GuiReceiverThread>(
                *gui_receiver_, target_selector_);
        }
    }
}

// Pipeline Worker를 실행한다.
bool RuntimeApplication::request_model_change(
    const std::string& model_config_path)
{
    if (model_config_path.empty())
        throw std::invalid_argument("model config path must not be empty");

    ModelConfig model = load_model_config(model_config_path);
    auto factory = DetectorFactory::for_model(model);
    if (!factory)
        throw std::runtime_error("Unsupported model configuration");

    std::lock_guard<std::mutex> lock(model_change_mutex_);
    if (runtime_state_.load() != RuntimeState::Running)
        return false;

    ModelChangeRequest request;
    request.request_id = next_model_request_id_++;
    request.config_path = model_config_path;
    request.model = std::move(model);

    runtime_state_.store(RuntimeState::Switching);
    if (!model_change_requests_.push(std::move(request)))
    {
        runtime_state_.store(RuntimeState::Running);
        return false;
    }

    Logger::info("[Runtime] 모델 변경 요청 수락됨");
    return true;
}

void RuntimeApplication::run()
{
    fs::path result_dir;
    g_stop_requested = 0;
    if (std::signal(SIGINT, handle_sigint) == SIG_ERR)
        throw std::runtime_error("SIGINT handler 등록에 실패했습니다");

    auto stop_pipeline = [this]() {
        camera_thread_->request_stop();
        frame_queue_.close();
        detection_queue_.close();
        control_track_queue_.close();
        gui_track_queue_.close();
        model_change_requests_.close();
        if (gui_receiver_thread_)
            gui_receiver_thread_->stop();
    };

    auto join_workers = [this]() {
        camera_thread_->join();
        detection_thread_->join();
        tracking_thread_->join();
        if (gui_sender_thread_) gui_sender_thread_->join();
        control_thread_->join();
        if (gui_receiver_thread_)
        {
            gui_receiver_thread_->stop();
            gui_receiver_thread_->join();
        }
    };

    try
    {
        if (measurement_enabled_)
        {
            result_dir = create_result_directory(
                config_.measurement.output_root,
                config_.model.name,
                runtime_mode_);
            Logger::info("[Measurement] 결과 경로: " + result_dir.string());

            // snapshot은 실행 디렉터리 바로 아래에 원본 파일명으로 보관한다.
            fs::copy_file(config_path_, result_dir / "runtime.yaml");
            fs::copy_file(
                config_.model_config_path,
                result_dir / fs::path(config_.model_config_path).filename());
        }

        // 수신·소비 worker를 먼저 대기시키고 Camera producer를 마지막에 시작한다.
        if (gui_receiver_thread_) gui_receiver_thread_->start();
        if (gui_sender_thread_) gui_sender_thread_->start();
        control_thread_->start();
        tracking_thread_->start();
        detection_thread_->start();
        camera_thread_->start();

        bool stop_logged = false;
        while (!camera_thread_->finished())
        {
            const bool worker_failed = detection_thread_->failed() ||
                tracking_thread_->failed() || control_thread_->failed() ||
                (gui_sender_thread_ && gui_sender_thread_->failed()) ||
                (gui_receiver_thread_ && gui_receiver_thread_->failed());

            if (worker_failed)
            {
                stop_pipeline();
                break;
            }

            if (g_stop_requested != 0)
            {
                // signal handler는 상태만 바꾸고 실제 종료 전파는 정상 흐름에서 한다.
                if (!stop_logged)
                {
                    Logger::info("[Runtime] Ctrl+C 종료 요청");
                    stop_logged = true;
                }
                camera_thread_->request_stop();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        join_workers();

        if (camera_thread_->failed())
            throw std::runtime_error(
                "CameraThread 실패: " + camera_thread_->error_message());
        if (detection_thread_->failed())
            throw std::runtime_error(
                "DetectionThread 실패: " + detection_thread_->error_message());
        if (tracking_thread_->failed())
            throw std::runtime_error(
                "TrackingThread 실패: " + tracking_thread_->error_message());
        if (control_thread_->failed())
            throw std::runtime_error(
                "ControlThread 실패: " + control_thread_->error_message());
        if (gui_sender_thread_ && gui_sender_thread_->failed())
            throw std::runtime_error(
                "GuiSenderThread 실패: " + gui_sender_thread_->error_message());
        if (gui_receiver_thread_ && gui_receiver_thread_->failed())
            throw std::runtime_error(
                "GuiReceiverThread 실패: " + gui_receiver_thread_->error_message());

        if (!measurement_enabled_)
            return;

        std::ofstream metrics_file(result_dir / "metrics.csv");
        if (!metrics_file)
            throw std::runtime_error("metrics.csv 파일을 생성하지 못했습니다");

        metrics_file << "frame_id,stage,queue_wait_ms,processing_ms,e2e_ms\n"
                     << std::fixed << std::setprecision(6);
        append_metrics(metrics_file, "camera", camera_thread_->metrics());
        append_metrics(metrics_file, "detection", detection_thread_->metrics());
        append_metrics(metrics_file, "tracking", tracking_thread_->metrics());
        if (config_.control.enabled)
            append_metrics(metrics_file, "control", control_thread_->metrics());

        if (!metrics_file)
            throw std::runtime_error("metrics.csv 파일 쓰기에 실패했습니다");
        metrics_file.close();
        if (metrics_file.fail())
            throw std::runtime_error("metrics.csv 파일을 저장하지 못했습니다");

        // 모든 Detection callback이 끝난 뒤 프레임별 내부 단계 시간을 저장한다.
        std::vector<DetectionTiming> detection_timings =
            detection_thread_->detection_timings();
        std::sort(
            detection_timings.begin(), detection_timings.end(),
            [](const DetectionTiming& lhs, const DetectionTiming& rhs)
            {
                return lhs.frame_id < rhs.frame_id;
            });

        std::ofstream detection_timing_file(
            result_dir / "detection_timing.csv");
        if (!detection_timing_file)
            throw std::runtime_error(
                "detection_timing.csv 파일을 생성하지 못했습니다");

        detection_timing_file
            << "frame_id,preprocess_ms,latency_ms,"
               "npu_processing_ms,postprocess_ms\n"
            << std::fixed << std::setprecision(6);
        for (const DetectionTiming& timing : detection_timings)
        {
            detection_timing_file
                << timing.frame_id << ','
                << timing.preprocess_ms() << ','
                << timing.latency_ms() << ','
                << timing.npu_processing_ms() << ','
                << timing.postprocess_ms() << '\n';
        }
        if (!detection_timing_file)
            throw std::runtime_error(
                "detection_timing.csv 파일 쓰기에 실패했습니다");
        detection_timing_file.close();
        if (detection_timing_file.fail())
            throw std::runtime_error(
                "detection_timing.csv 파일을 저장하지 못했습니다");

        Logger::info("[Measurement] 측정 결과 저장 완료: " + result_dir.string());
    }
    catch (...)
    {
        stop_pipeline();
        join_workers();

        if (!result_dir.empty())
        {
            // 실패한 실행만 제거하고 model/tracker 및 mode 상위 디렉터리는 보존한다.
            std::error_code cleanup_error;
            fs::remove_all(result_dir, cleanup_error);
            if (cleanup_error)
                Logger::error(
                    "[Runtime] 실패 결과 정리에 실패했습니다: " +
                    result_dir.string() + " (" + cleanup_error.message() + ")");
            else
                Logger::info("[Runtime] 실패 결과 정리: " + result_dir.string());
        }
        throw;
    }
}

int run_runtime(const std::string& config_path)
{
    RuntimeApplication app(config_path);
    app.run();
    return 0;
}
