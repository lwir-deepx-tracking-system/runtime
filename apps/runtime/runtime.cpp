#include "app/camera_watchdog.hpp"
#include "runtime.hpp"
#include "common/pipeline_performance.hpp"
#include "auto_model_coordinator.hpp"
#include "model_change_requests.hpp"
#include "recording_archive.hpp"
#include "encoder_cpu_monitor.hpp"
#include "app/pipeline_console.hpp"
#include "app/metrics_writer.hpp"
#include "app/raw_recorder.hpp"
#include "app/shutdown.hpp"
#include "runtime_workers.hpp"
#include "common/logger.hpp"
#include "common/frame.hpp"
#include "common/threadsafequeue.hpp"
#include "common/detection.hpp"
#include "common/track.hpp"

#include "detection/detector_factory.hpp"
#include "detection/auto_model_selector.hpp"
#include "tracking/tracker_factory.hpp"
#include "tracking/tracker_manager.hpp"

#include "camera/camera.hpp"
#include "gui/gui_receiver.hpp"
#ifdef LWIR_HAS_WEB_GUI
#include "gui/web_server.hpp"
#endif
#include "app/pipeline_requests.hpp"
#include "gui/gui_sender.hpp"
#include "tracking/tracker.hpp"

#include "pipeline/model_manager_thread.hpp"
#include "pipeline/tracker_change_service.hpp"
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
#include <cmath>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>


// Runtime 구성 요소를 소유하고 실행 중 모델·트래커 교체 요청을 받는다.
// Pipeline 실행을 관리하는 최상위 Application 클래스
class RuntimeApplication : public IPipelineRequests
{
private:
    RuntimeConfig config_;
    std::filesystem::path recording_root() const
    {
        return std::filesystem::path(config_.measurement.output_root) / "recordings";
    }
    std::unique_ptr<PipelinePerformance> performance_;
    std::unique_ptr<MetricsWriter> metrics_writer_;
    std::unique_ptr<RawRecorder> recorder_;
    std::unique_ptr<FrameTelemetryCollector> telemetry_collector_;
    IMeasurementSink* measurement_sink_ = nullptr;

    // Camera의 shared FrameContext를 Detection으로 전달
    ThreadSafeQueue<FrameMessage> frame_queue_;

    // Detection 목록 다음 Stage로 전달
    ThreadSafeQueue<DetectionResult> detection_queue_;
    AutoModelCoordinator auto_models_;

    // 같은 TrackingResult를 제어와 GUI 경로가 공유한다.
    ThreadSafeQueue<TrackingResultPtr> control_track_queue_;
    ThreadSafeQueue<TrackingResultPtr> gui_track_queue_;

    std::string config_path_;
    std::string runtime_mode_;
    std::string tracker_name_;
    bool measurement_enabled_ = false;
    bool console_enabled_ = false;
    bool encoder_cpu_enabled_ = false;
    RuntimeControl runtime_control_;
    ActivePipelineSlot active_pipeline_;
    ThreadSafeQueue<ModelChangeRequest> model_change_requests_;
    ThreadSafeQueue<ModelChangeResult> model_change_results_;
    ModelChangeRequests model_requests_;
    std::atomic<bool> pipeline_ready_{false};
/************************************************************************/
    // Camera 객체
    std::unique_ptr<Camera> camera_;

    // Camera thread
    std::unique_ptr<CameraThread> camera_thread_;

    // ModelManager owns model lifecycle; DetectionThread only borrows a snapshot.
    std::unique_ptr<ModelManagerThread> model_manager_thread_;
    std::unique_ptr<DetectionThread> detection_thread_;

    // 활성 트래커와 snapshot의 수명은 TrackerManager가 관리한다.
    std::unique_ptr<TrackerManager> tracker_manager_;

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
    // 콜백 대상인 짐벌과 manager보다 먼저 파괴해 작업 스레드를 합류시킨다.
    std::unique_ptr<TrackerChangeService> tracker_changes_;
    std::unique_ptr<RuntimeWorkers> workers_;

public:
    // YAML 설정을 읽고 필요한 객체를 생성
    explicit RuntimeApplication(const std::string& config_path, bool console_enabled = false,
        bool encoder_cpu_enabled = false);
    RuntimeApplication(const RuntimeApplication&) = delete;
    RuntimeApplication& operator=(const RuntimeApplication&) = delete;

    // Pipeline Worker 실행
    std::uint32_t model_count() const override { return config_.detection_catalog.size(); }
    std::uint32_t tracker_count() const override { return config_.tracking_catalog.size(); }
    PipelineCatalogItem model_info(std::uint32_t id) const override;
    PipelineCatalogItem tracker_info(std::uint32_t id) const override;
    PipelineTrackerPolicy tracker_policy() const override;
    PipelineAutoDetection auto_detection() const override;
    bool set_auto_detection(bool enabled) override;
    bool set_auto_detection_thresholds(float far_px,float near_px) override;
    NpuFeedState npu_feed_state() const override { return active_pipeline_.npu_feed_state(); }
    bool set_npu_feed(std::uint8_t mode) override { return active_pipeline_.set_npu_feed(mode); }
    bool request_camera_reopen() override {
        return camera_thread_ && camera_thread_->request_reopen();
    }
    bool recording_available() const override { return recorder_ != nullptr; }
    PipelineRecordingStatus recording_status() const override;
    bool start_recording(const std::string& name) override;
    bool stop_recording() override;
    std::string latest_recording() const override;
    std::vector<std::string> list_recordings() const override;
    PipelineRecordingFileChunk read_recording_file(
        const std::string&, std::uint8_t, std::uint64_t, std::uint16_t) const override;
    PipelineChangeTicket request_model_change(std::uint32_t model_id) override;
    // 트래커 변경 서비스에 요청을 위임한다.
    PipelineChangeTicket request_tracker_change(std::uint32_t tracker_id) override;
    PipelineChangeTicket request_policy_change(const TrackerPolicyUpdate& policy) override;
    void run();
};


// YAML을 읽고 설정에 맞는 Component들을 생성한다.
RuntimeApplication::RuntimeApplication(const std::string& config_path, bool console_enabled,
    bool encoder_cpu_enabled)
    : config_(load_runtime_config(config_path)), auto_models_(config_), config_path_(config_path),
      console_enabled_(console_enabled), encoder_cpu_enabled_(encoder_cpu_enabled)
{
#ifndef LWIR_HAS_WEB_GUI
    if (config_.gui.enabled && config_.gui.mode == GuiMode::Web)
        throw std::runtime_error("gui.mode=web requires a build with LWIR_BUILD_WEB_GUI=ON");
#endif
    if (encoder_cpu_enabled_ && (!config_.gui.enabled ||
        config_.gui.video.codec != GuiVideoCodec::H264))
        throw std::runtime_error("--encoder-cpu requires enabled H.264 GUI video");
    Logger::set_level(config_.logging.level);
    const bool telemetry_enabled = config_.gui.enabled && config_.gui.metadata.enabled &&
        config_.gui.metadata.telemetry_enabled;
    measurement_enabled_ = config_.measurement.enabled || telemetry_enabled;
    if (config_.measurement.enabled) performance_ = std::make_unique<PipelinePerformance>();

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

    if (config_.measurement.enabled) {
        MetricsWriterOptions options;
        options.output_root = std::filesystem::path(config_.measurement.output_root) / runtime_mode_;
        options.snapshots.emplace_back(config_path_, "runtime.yaml");
        options.snapshots.emplace_back(config_.reid.config_path,
            std::filesystem::path("reid") /
                std::filesystem::path(config_.reid.config_path).filename());
        options.snapshots.emplace_back(config_.reid.matcher_config_path,
            std::filesystem::path("reid") /
                std::filesystem::path(config_.reid.matcher_config_path).filename());
        for (const auto& [id, entry] : config_.detection_catalog)
            options.model_configs.emplace(id, entry.config_path);
        for (const auto& [id, entry] : config_.tracking_catalog)
            options.snapshots.emplace_back(entry.config_path,
                std::filesystem::path("tracking") / (std::to_string(id) + ".yaml"));
        metrics_writer_ = std::make_unique<MetricsWriter>(options);
        Logger::info("[Measurement] 결과 경로: " + metrics_writer_->directory().string());
    }
    if (telemetry_enabled)
        telemetry_collector_ = std::make_unique<FrameTelemetryCollector>(metrics_writer_.get());
    measurement_sink_ = telemetry_collector_ ? static_cast<IMeasurementSink*>(telemetry_collector_.get()) :
        static_cast<IMeasurementSink*>(metrics_writer_.get());
    camera_ = std::make_unique<Camera>();
    if (config_.measurement.recording_enabled)
        recorder_ = std::make_unique<RawRecorder>(
            recording_root());

    model_manager_thread_ = std::make_unique<ModelManagerThread>(
        config_.detection_catalog.at(config_.initial_detection_id),
        config_.detection.inference,
        model_change_requests_,
        active_pipeline_,
        runtime_control_, &model_change_results_,
        std::function<std::shared_ptr<IDetectionPipeline>(const ModelCatalogEntry&)>{},
        measurement_sink_,
        config_.resident_detection_pair ? std::optional<ModelCatalogEntry>(
            config_.detection_catalog.at(config_.initial_detection_id == config_.auto_near_model_id ?
                config_.auto_far_model_id : config_.auto_near_model_id)) : std::nullopt);


    auto tracker_factory = TrackerFactory::for_type(config_.tracking.type);
    tracker_name_ = tracker_factory->name();
    if (config_.tracking_catalog.size() > 1)
        tracker_name_ = "switchable";
    tracker_manager_ = std::make_unique<TrackerManager>(
        config_.tracking_catalog, config_.initial_tracker_id,
        [this](const TrackingConfig& tracking) -> std::shared_ptr<Tracker> {
            return TrackerFactory::create_configured(tracking,config_.reid,
                config_.detection.model.camera_input,config_.reid_options);
        }, &runtime_control_.tracker_epoch);

    
    // Camera Thread에 연결
    // Camera → frame_queue
    camera_thread_ =
        std::make_unique<CameraThread>(
            *camera_,
            frame_queue_,
            measurement_enabled_, false, recorder_.get(), performance_.get(), config_.camera_recovery
        );

    detection_thread_ =
        std::make_unique<DetectionThread>(
            runtime_control_,
            active_pipeline_,
            frame_queue_,
            detection_queue_,
            gui_track_queue_,
            config_.gui.enabled,
            measurement_enabled_, measurement_sink_
        );
    
    // detection_queue → Tracking → track_queue
    tracking_thread_ =
        std::make_unique<TrackingThread>(
            *tracker_manager_,
            runtime_control_,
            detection_queue_,
            control_track_queue_,
            gui_track_queue_,
            measurement_enabled_, measurement_sink_, recorder_.get(),
            auto_models_.observations(), performance_.get()
        );

    // AppConfig가 검증한 제어 활성 상태에서만 실제 장치와 실행기를 생성한다.
    if (config_.control.enabled)
        gimbal_executor_ = std::make_unique<GimbalCommandExecutor>(
            config_.control, GimbalDeviceFactory::create(config_.control.gimbal), runtime_control_);
    control_thread_ = std::make_unique<ControlThread>(
        control_track_queue_, gimbal_executor_.get(), measurement_enabled_, measurement_sink_);


    if (config_.gui.enabled)
    {
        gui_sender_ = std::make_unique<GuiSender>(
            config_.gui.video,
            config_.gui.metadata,
            config_.detection.model.camera_input.clip_min,
            config_.detection.model.camera_input.clip_max, telemetry_collector_.get());
        gui_sender_thread_ = std::make_unique<GuiSenderThread>(
            *gui_sender_, runtime_control_, gui_track_queue_);

        if (config_.gui.command.enabled)
        {
            gui_receiver_ = std::make_unique<GuiReceiver>(config_.gui.command,
                GuiCommandHandlerFactory::create(config_.gui.command.handler, config_.control, gimbal_executor_.get(), this));
            gui_receiver_thread_ = std::make_unique<GuiReceiverThread>(
                *gui_receiver_);
        }
    }
    tracker_changes_ = std::make_unique<TrackerChangeService>(*tracker_manager_, [this] {
        if (gimbal_executor_) gimbal_executor_->suspend();
    });
    workers_ = std::make_unique<RuntimeWorkers>(
        runtime_control_, frame_queue_, model_change_requests_,
        *camera_thread_, *detection_thread_, *model_manager_thread_,
        *tracking_thread_, *control_thread_, gimbal_executor_.get(),
        gui_sender_thread_.get(), gui_receiver_thread_.get(), *tracker_changes_);
}

// GUI 수신 스레드에서 요청을 접수하고 모델 매니저의 완료 결과와 future를 연결한다.
PipelineCatalogItem RuntimeApplication::model_info(std::uint32_t id) const
{
    const auto it = config_.detection_catalog.find(static_cast<int>(id));
    if (it == config_.detection_catalog.end()) return {};
    const auto& entry = it->second;
    return {id, entry.display_name.empty() ? entry.model.name : entry.display_name,
            std::filesystem::is_regular_file(entry.model.path),
            model_manager_thread_ && model_manager_thread_->active_model_id() == static_cast<int>(id)};
}

PipelineCatalogItem RuntimeApplication::tracker_info(std::uint32_t id) const
{
    const auto it = config_.tracking_catalog.find(static_cast<int>(id));
    if (it == config_.tracking_catalog.end()) return {};
    const auto& entry = it->second;
    const bool needs_reid = entry.config.post_reid || entry.config.type == TrackerType::BotSort;
    const bool available = !needs_reid || std::filesystem::is_regular_file(config_.reid.model.path);
    return {id, entry.display_name.empty() ? "Tracker " + std::to_string(id) : entry.display_name,
            available, tracker_manager_ && tracker_manager_->active_id() == static_cast<int>(id)};
}

PipelineTrackerPolicy RuntimeApplication::tracker_policy() const
{
    if (!tracker_manager_) return {};
    const auto [active, c] = tracker_manager_->policy_snapshot();
    const auto id = active.id;
    if (id <= 0) return {};
    const auto effective_frames = active.tracker ? active.tracker->current_confirmation_frames() : 0;
    return {static_cast<std::uint32_t>(id),
            static_cast<std::uint32_t>(c.frame.warmth_min),
            static_cast<std::uint32_t>(c.frame.warmth_max),
            c.edge_birth_threshold,
            static_cast<std::uint32_t>(effective_frames > 0 ? effective_frames : c.confirmation_frames),
            static_cast<std::uint32_t>(c.frame.confirmation_ms),
            (c.class_gate ? 1u : 0u) | (c.frame.gmc ? 2u : 0u) |
                (c.post_reid ? 4u : 0u),
            c.position_memory=="gru_kpt" ? 1u : c.position_memory=="mamba_kpt" ? 2u :
                c.position_memory=="gru_near_kpt" ? 3u : 0u,
            c.box_refine.mode=="size" ? 1u : c.box_refine.mode=="body" ? 2u : 0u,
            c.box_refine.lost_same,static_cast<float>(c.box_refine.aim_y)};
}

PipelineAutoDetection RuntimeApplication::auto_detection() const { return auto_models_.auto_detection(); }
bool RuntimeApplication::set_auto_detection(bool enabled) { return auto_models_.set_auto_detection(enabled); }
bool RuntimeApplication::set_auto_detection_thresholds(float far_px,float near_px)
{
    return auto_models_.set_auto_detection_thresholds(far_px, near_px);
}

PipelineRecordingStatus RuntimeApplication::recording_status() const
{
    if (!recorder_) return {};
    const auto s = recorder_->status();
    return {s.active, s.failed, s.session, s.frames, s.dropped};
}

bool RuntimeApplication::start_recording(const std::string& name)
{
    return recorder_ && pipeline_ready_.load() && recorder_->start(name);
}

bool RuntimeApplication::stop_recording()
{
    return recorder_ && recorder_->stop();
}

std::string RuntimeApplication::latest_recording() const
{
    const auto names = list_recordings();
    return names.empty() ? std::string{} : names.front();
}

std::vector<std::string> RuntimeApplication::list_recordings() const
{
    return recorder_ ? RecordingArchive(*recorder_, recording_root()).list_recordings()
        : std::vector<std::string>{};
}

PipelineRecordingFileChunk RuntimeApplication::read_recording_file(
    const std::string& name, std::uint8_t file_id, std::uint64_t offset, std::uint16_t limit) const
{
    return recorder_ ? RecordingArchive(*recorder_, recording_root()).read_recording_file(name, file_id, offset, limit)
        : PipelineRecordingFileChunk{};
}

PipelineChangeTicket RuntimeApplication::request_model_change(std::uint32_t model_id)
{
    return model_requests_.request(model_id, config_.detection_catalog, pipeline_ready_,
        runtime_control_, frame_queue_, model_change_requests_,
        [this] { return model_manager_thread_->active_model_id(); });
}

// GUI와 콘솔 요청은 같은 서비스의 접수·완료 경로를 사용한다.
PipelineChangeTicket RuntimeApplication::request_tracker_change(std::uint32_t tracker_id)
{
    if (!pipeline_ready_.load() || runtime_control_.stop_requested.load())
        return {PipelineResult::NotReady, {}};
    return tracker_changes_->request_change(tracker_id);
}

PipelineChangeTicket RuntimeApplication::request_policy_change(const TrackerPolicyUpdate& policy)
{
    if (!pipeline_ready_.load() || runtime_control_.stop_requested.load())
        return {PipelineResult::NotReady, {}};
    return tracker_changes_->request_policy_change(policy);
}

void RuntimeApplication::run()
{
    app_shutdown::SignalScope signals;
#ifdef LWIR_HAS_WEB_GUI
    std::unique_ptr<web_gui::WebServer> web_server;
#endif

    bool results_discarded = false;
    // SIGQUIT은 이전 실행을 보존하고 현재 실행의 큐와 저장 결과만 폐기한다.
    auto discard_if_requested = [this, &results_discarded]() {
        if (app_shutdown::discard_results && !results_discarded) {
            if (metrics_writer_) metrics_writer_->discard();
            results_discarded = true;
        }
    };

    auto report_model_changes = [this]() {
        ModelChangeResult result;
        while (model_change_results_.try_pop(result))
        {
            if (metrics_writer_ && !app_shutdown::discard_results) metrics_writer_->flush();
            model_requests_.complete(result);
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
        workers_->start();
        pipeline_ready_.store(true);
#ifdef LWIR_HAS_WEB_GUI
        if (config_.gui.enabled && config_.gui.mode == GuiMode::Web)
            web_server = std::make_unique<web_gui::WebServer>(config_, recording_root());
#endif
        CameraWatchdog::ready();
        std::unique_ptr<PipelineConsole> console;
        if (console_enabled_) console = std::make_unique<PipelineConsole>(*this, std::cout);

        EncoderCpuMonitor encoder_cpu;
        if (encoder_cpu_enabled_)
            std::cout << "[EncoderCPU] encoder=" << config_.gui.video.encoder
                      << " bitrate_kbps=" << config_.gui.video.bitrate_kbps
                      << " fps=" << config_.gui.video.fps
                      << " gop=" << config_.gui.video.keyframe_interval
                      << " rtp_mtu=" << config_.gui.video.rtp_mtu
                      << " warmup_s=10 window_s=60; waiting for first RTP frame\n";

        while (!camera_thread_->finished())
        {
            if (console && !console->poll()) app_shutdown::request_stop();
            if (app_shutdown::requested) {
                pipeline_ready_.store(false);
                discard_if_requested();
                Logger::info(app_shutdown::discard_results ?
                    "[Runtime] SIGQUIT: 이번 실행 결과 폐기 후 종료" :
                    "[Runtime] 측정 결과 저장 후 종료 요청");
                workers_->stop();
                break;
            }
            if (model_requests_.take_suspend_request() && gimbal_executor_) gimbal_executor_->suspend();
            report_model_changes();
            auto_models_.poll(runtime_control_, gimbal_executor_.get(), model_manager_thread_->active_model_id(),
                [this](std::uint32_t id) { return request_model_change(id); });
            const bool worker_failed = (metrics_writer_ && metrics_writer_->failed()) || detection_thread_->failed() ||
                (model_manager_thread_ && model_manager_thread_->failed()) ||
                tracking_thread_->failed() || control_thread_->failed() ||
                (gui_sender_thread_ && gui_sender_thread_->failed()) ||
                (gui_receiver_thread_ && gui_receiver_thread_->failed());

#ifdef LWIR_HAS_WEB_GUI
            if (web_server && web_server->failed())
                throw std::runtime_error("Web GUI listener stopped unexpectedly");
#endif
            if (worker_failed || runtime_control_.stop_requested.load())
            {
                workers_->stop();
                break;
            }

            if (encoder_cpu_enabled_)
            {
                timespec cpu{};
                if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu) != 0)
                    throw std::runtime_error("Cannot read process CPU time");
                const double wall = std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                const auto sample = encoder_cpu.poll(wall,
                    cpu.tv_sec + cpu.tv_nsec / 1e9, gui_sender_->stats());
                if (sample)
                {
                    // Use one line, independently of the configured log level.
                    // Capture with tee; no extra writer thread or disk I/O in workers.
                    std::ostringstream line;
                    line << std::fixed << std::setprecision(3)
                         << "[EncoderCPU] encoder=" << config_.gui.video.encoder
                         << " elapsed_s=" << sample->elapsed_seconds
                         << " interval_s=" << sample->interval_seconds
                         << " cpu_percent_one_core=" << sample->cpu_percent_one_core
                         << " avg_cpu_percent_one_core=" << sample->average_cpu_percent_one_core
                         << " submitted_fps=" << sample->submitted_fps
                         << " tx_fps=" << sample->tx_fps
                         << " avg_tx_fps=" << sample->average_tx_fps
                         << " submitted=" << sample->submitted
                         << " encoded=" << sample->encoded
                         << " final=" << sample->final;
                    std::cout << line.str() << std::endl;
                    if (sample->final) encoder_cpu_enabled_ = false;
                }
            }
            if (performance_) {
                const auto report = performance_->report();
                if (!report.empty()) Logger::info(report);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        pipeline_ready_.store(false);
#ifdef LWIR_HAS_WEB_GUI
        web_server.reset();
#endif
        tracker_changes_->close();
        discard_if_requested();
        workers_->join();
        discard_if_requested();
        report_model_changes();
        model_requests_.cancel_pending();
        if (metrics_writer_) {
            if (!results_discarded) metrics_writer_->close();
            discard_if_requested(); // close 도중 도착한 SIGQUIT도 저장 종료로 바뀌지 않는다.
            if (!results_discarded && metrics_writer_->failed())
                throw std::runtime_error("Measurement writer 실패: " + metrics_writer_->error_message());
        }

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

        if (metrics_writer_ && !results_discarded)
            Logger::info("[Measurement] 측정 결과 저장 완료: " + metrics_writer_->directory().string());
    }
    catch (...)
    {
        pipeline_ready_.store(false);
#ifdef LWIR_HAS_WEB_GUI
        web_server.reset();
#endif
        tracker_changes_->close();
        workers_->stop();
        workers_->join();
        model_requests_.cancel_pending();

        // SIGQUIT은 오류 경로에서도 결과를 폐기하고, 그 외 오류는 기록을 보존한다.
        discard_if_requested();
        if (metrics_writer_ && !results_discarded) {
            metrics_writer_->close();
            discard_if_requested();
        }
        throw;
    }
}

int run_runtime(const std::string& config_path, bool console_enabled, bool encoder_cpu_enabled)
{
    try {
        RuntimeApplication app(config_path, console_enabled, encoder_cpu_enabled);
        app.run();
        return 0;
    } catch (const std::exception& error) {
        Logger::error(std::string("[Runtime] ") + error.what());
        return 1;
    }
}
