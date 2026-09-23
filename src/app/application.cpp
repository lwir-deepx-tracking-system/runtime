#include "app/application.hpp"
#include "common/logger.hpp"

#include "app/app_config.hpp"
#include "factory/component_factory.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

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

// YAML을 읽고 설정에 맞는 Component들을 생성한다.
Application::Application(const std::string& config_path)
    : config_path_(config_path)
{
    // YAML 로그 레벨을 먼저 적용한 뒤 초기 큐 상태를 출력한다.
    AppConfig config = load_config(config_path);
    measurement_enabled_ = config.measurement.enabled;

    Logger::debug("[Application] 큐 생성 완료");

    Logger::debug(
        "[Application] frame_queue 크기: " +
        std::to_string(frame_queue_.size())
    );

    Logger::debug(
        "[Application] track_queue 크기: " +
        std::to_string(track_queue_.size())
    );

    Logger::debug(
        "[Application] target_selection_queue 크기: " +
        std::to_string(target_selection_queue_.size())
    );

    // 결과 폴더에는 실행 당시 참조한 YAML을 사본으로 남긴다.
    config_snapshot_paths_ = {
        config_path_,
        config.detector.config_path,
        config.tracking.config_path
    };
    if (config.control.enabled)
        config_snapshot_paths_.push_back(config.control.config_path);

    camera_ = std::make_unique<Camera>();

    detection_pipeline_ = ComponentFactory::create_detection_pipeline(config);

    // Tracking 구현체 생성
    tracker_ = ComponentFactory::create_tracker(config);

    
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
            track_queue_,
            measurement_enabled_
        );

    // Tracking 결과와 GUI 선택 ID를 대상 선택 단계에 연결
    target_selection_thread_ =
        std::make_unique<TargetSelectionThread>(
            target_selector_,
            track_queue_,
            target_selection_queue_,
            measurement_enabled_
        );

    // 선택 결과를 Orange Pi 짐벌 제어 단계에 연결
    control_thread_ =
        std::make_unique<ControlThread>(
            gimbal_controller_,
            target_selection_queue_,
            config.control.enabled,
            measurement_enabled_
        );
}

// GUI 입력을 대상 선택 단계에 전달한다.
void Application::set_selected_track_id(int track_id)
{
    target_selector_.set_selected_id(track_id);
}


// Pipeline Worker를 실행한다.
void Application::run()
{
    control_thread_->start();
    target_selection_thread_->start();
    // Tracking 스레디 시작
    tracking_thread_->start();
    detection_thread_->start();
    // 마지막에 Frame 생산자 시작
    camera_thread_->start();


    // 각 Thread 종료 대기
    camera_thread_->join();
    detection_thread_->join();
    tracking_thread_->join();
    target_selection_thread_->join();
    control_thread_->join();

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
    append_metrics(metrics_file, "target_selection", target_selection_thread_->metrics());
    append_metrics(metrics_file, "control", control_thread_->metrics());
    metrics_file.close();
    if (!metrics_file)
        throw std::runtime_error("metrics.csv 파일을 저장하지 못했습니다");

    Logger::info("[Measurement] 측정 결과 저장 완료: " + result_dir.string());
}
