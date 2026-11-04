#include "runtime.hpp"
#include "runtime_metrics.hpp"
#include "runtime_workers.hpp"
#include "common/logger.hpp"
#include "common/frame.hpp"
#include "common/threadsafequeue.hpp"
#include "common/detection.hpp"
#include "common/track.hpp"

#include "detection/detector_factory.hpp"
#include "tracking/tracker_factory.hpp"

#include "camera/camera.hpp"
#include "gui/gui_receiver.hpp"
#include "gui/gui_sender.hpp"
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

namespace fs = std::filesystem;

namespace {
volatile std::sig_atomic_t g_stop_requested = 0;

void handle_sigint(int)
{
    g_stop_requested = 1;
}

}  // namespace

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
    std::string tracker_name_;
    bool measurement_enabled_ = false;
    RuntimeControl runtime_control_;
    ActivePipelineSlot active_pipeline_;
    std::mutex model_change_mutex_;
    std::uint64_t next_model_request_id_ = 1;
    ThreadSafeQueue<ModelChangeRequest> model_change_requests_;
    ThreadSafeQueue<ModelChangeResult> model_change_results_;

/************************************************************************/
    // Camera 객체
    std::unique_ptr<Camera> camera_;

    // Camera thread
    std::unique_ptr<CameraThread> camera_thread_;

    // ModelManager owns model lifecycle; DetectionThread only borrows a snapshot.
    std::unique_ptr<ModelManagerThread> model_manager_thread_;
    std::unique_ptr<DetectionThread> detection_thread_;

    // 현재 사용하는 Tracker 객체를 Application이 소유한다.
    std::unique_ptr<Tracker> tracker_;

    // Tracking thread
    std::unique_ptr<TrackingThread> tracking_thread_;

    std::unique_ptr<GimbalCommandExecutor> gimbal_executor_;
    std::unique_ptr<ControlThread> control_thread_;

    // Application이 GUI 통신 객체의 전체 수명을 소유한다. Sender 내부의
    // GStreamer pipeline과 Receiver 내부의 TCP socket은 각 객체가 RAII로
    // 정리하고, worker는 실행 thread만 담당한다.
    std::unique_ptr<GuiSender> gui_sender_;
    std::unique_ptr<GuiSenderThread> gui_sender_thread_;
    std::unique_ptr<GuiReceiver> gui_receiver_;
    std::unique_ptr<GuiReceiverThread> gui_receiver_thread_;
    std::unique_ptr<RuntimeWorkers> workers_;


public:
    // YAML 설정을 읽고 필요한 객체를 생성
    explicit RuntimeApplication(const std::string& config_path);

    // Pipeline Worker 실행
    bool request_model_change(int model_id);
    void run();
};


// YAML을 읽고 설정에 맞는 Component들을 생성한다.
RuntimeApplication::RuntimeApplication(const std::string& config_path)
    : config_(load_runtime_config(config_path)), config_path_(config_path)
{
    Logger::set_level(config_.logging.level);
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

    model_manager_thread_ = std::make_unique<ModelManagerThread>(
        config_.model_catalog.at(config_.initial_model_id),
        config_.detection,
        model_change_requests_,
        active_pipeline_,
        runtime_control_, &model_change_results_);

    auto tracker_factory = TrackerFactory::for_type(config_.tracking.type);
    tracker_name_ = tracker_factory->name();
    tracker_ = tracker_factory->create(config_.tracking);

    
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
            runtime_control_,
            active_pipeline_,
            frame_queue_,
            detection_queue_,
            gui_track_queue_,
            config_.gui.enabled,
            measurement_enabled_
        );
    
    // detection_queue → Tracking → track_queue
    tracking_thread_ =
        std::make_unique<TrackingThread>(
            *tracker_,
            runtime_control_,
            detection_queue_,
            control_track_queue_,
            gui_track_queue_,
            measurement_enabled_
        );

    if (config_.gui.command.handler == GuiCommandHandlerType::Tracking)
        gimbal_executor_ = std::make_unique<GimbalCommandExecutor>(
            config_.control, GimbalDeviceFactory::create(config_.control.gimbal), runtime_control_);
    control_thread_ = std::make_unique<ControlThread>(
        control_track_queue_, gimbal_executor_.get(), measurement_enabled_);


    if (config_.gui.enabled)
    {
        gui_sender_ = std::make_unique<GuiSender>(
            config_.gui.video,
            config_.gui.metadata,
            config_.model.camera_input.clip_min,
            config_.model.camera_input.clip_max);
        gui_sender_thread_ = std::make_unique<GuiSenderThread>(
            *gui_sender_, runtime_control_, gui_track_queue_);

        if (config_.gui.command.enabled)
        {
            gui_receiver_ = std::make_unique<GuiReceiver>(config_.gui.command,
                GuiCommandHandlerFactory::create(config_.gui.command.handler, config_.control, gimbal_executor_.get()));
            gui_receiver_thread_ = std::make_unique<GuiReceiverThread>(
                *gui_receiver_);
        }
    }
    workers_ = std::make_unique<RuntimeWorkers>(
        runtime_control_, frame_queue_, model_change_requests_,
        *camera_thread_, *detection_thread_, *model_manager_thread_,
        *tracking_thread_, *control_thread_, gimbal_executor_.get(),
        gui_sender_thread_.get(), gui_receiver_thread_.get());
}

// Pipeline Worker를 실행한다.
bool RuntimeApplication::request_model_change(int model_id)
{
    const auto model = config_.model_catalog.find(model_id);
    if (model == config_.model_catalog.end())
    {
        Logger::error("[Runtime] rejected unknown model id=" +
            std::to_string(model_id));
        return false;
    }

    std::lock_guard<std::mutex> lock(model_change_mutex_);
    const RuntimeState state = runtime_control_.state.load();
    if (state != RuntimeState::Running &&
        state != RuntimeState::SwitchFailed)
        return false;
    if (state == RuntimeState::Running &&
        model_manager_thread_ &&
        model_manager_thread_->active_model_id() == model_id)
        return true;

    ModelChangeRequest request;
    request.request_id = next_model_request_id_++;
    request.entry = model->second;

    runtime_control_.set_detection_quiesced(false);
    runtime_control_.state.store(RuntimeState::Switching);
    frame_queue_.interrupt_waiters();
    if (gimbal_executor_) gimbal_executor_->suspend();
    if (!model_change_requests_.push(std::move(request)))
    {
        runtime_control_.state.store(state);
        return false;
    }

    Logger::info("[Runtime] model change requested id=" +
        std::to_string(model_id));
    return true;
}

void RuntimeApplication::run()
{
    fs::path result_dir;
    g_stop_requested = 0;
    if (std::signal(SIGINT, handle_sigint) == SIG_ERR ||
        std::signal(SIGTERM, handle_sigint) == SIG_ERR)
        throw std::runtime_error("Runtime signal handler 등록에 실패했습니다");

    auto report_model_changes = [this]() {
        ModelChangeResult result;
        while (model_change_results_.try_pop(result))
        {
            const char* outcome = result.outcome == ModelChangeOutcome::Activated ?
                "activated" : result.outcome == ModelChangeOutcome::Restored ?
                "restored" : "failed";
            Logger::info("[Runtime] model request=" +
                std::to_string(result.request_id) + " requested=" +
                std::to_string(result.requested_model_id) + " active=" +
                std::to_string(result.active_model_id) + " outcome=" + outcome +
                (result.error.empty() ? "" : " error=" + result.error));
        }
    };

    try
    {
        if (measurement_enabled_)
        {
            result_dir = RuntimeMetricsWriter::create_directory(
                config_.measurement.output_root,
                config_.model.name,
                tracker_name_,
                runtime_mode_);
            Logger::info("[Measurement] 결과 경로: " + result_dir.string());

            // snapshot은 실행 디렉터리 바로 아래에 원본 파일명으로 보관한다.
            fs::copy_file(config_path_, result_dir / "runtime.yaml");
            fs::copy_file(
                config_.model_config_path,
                result_dir / fs::path(config_.model_config_path).filename());
            const fs::path postprocess_dir = result_dir / "postprocess";
            fs::create_directory(postprocess_dir);
            for (const auto& [id, entry] : config_.postprocess_catalog)
                fs::copy_file(entry.config_path,
                    postprocess_dir / (std::to_string(id) + "_" +
                        fs::path(entry.config_path).filename().string()));
        }

        workers_->start();

        bool stop_logged = false;
        while (!camera_thread_->finished())
        {
            report_model_changes();
            const bool worker_failed = detection_thread_->failed() ||
                (model_manager_thread_ && model_manager_thread_->failed()) ||
                tracking_thread_->failed() || control_thread_->failed() ||
                (gui_sender_thread_ && gui_sender_thread_->failed()) ||
                (gui_receiver_thread_ && gui_receiver_thread_->failed());

            if (worker_failed)
            {
                workers_->stop();
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
                workers_->stop();
                measurement_enabled_ = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        workers_->join();
        report_model_changes();

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

        RuntimeMetricsWriter::write(result_dir,
            camera_thread_->metrics(), detection_thread_->metrics(),
            tracking_thread_->metrics(),
            config_.control.enabled ? &control_thread_->metrics() : nullptr,
            detection_thread_->detection_timings());

        Logger::info("[Measurement] 측정 결과 저장 완료: " + result_dir.string());
    }
    catch (...)
    {
        workers_->stop();
        workers_->join();

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
    try {
        RuntimeApplication app(config_path);
        app.run();
        return 0;
    } catch (const std::exception& error) {
        Logger::error(std::string("[Runtime] ") + error.what());
        return 1;
    }
}
