#include "pipeline/tracking_thread.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

#include "common/logger.hpp"

// 활성 TrackerManager와 입출력 queue를 Tracking worker에 연결한다.
TrackingThread::TrackingThread(
    TrackerManager& tracker_manager,
    RuntimeControl& runtime_control,
    ThreadSafeQueue<DetectionResult>& input_queue,
    ThreadSafeQueue<TrackingResultPtr>& control_output_queue,
    ThreadSafeQueue<TrackingResultPtr>& gui_output_queue,
    bool measurement_enabled, IMeasurementSink* measurements)
    : tracker_manager_(tracker_manager),
      runtime_control_(runtime_control),
      input_queue_(input_queue),
      control_output_queue_(control_output_queue),
      gui_output_queue_(gui_output_queue),
      measurement_enabled_(measurement_enabled), measurements_(measurements)
{
}

void TrackingThread::start()
{
    if (started_) return;
    thread_ = std::thread(&TrackingThread::thread_func, this);
    started_ = true;
}

void TrackingThread::join()
{
    if (!started_) return;
    if(thread_.joinable())thread_.join();
    started_ = false;
}

void* TrackingThread::thread_func(void* arg)
{
    static_cast<TrackingThread*>(arg)->run();
    return nullptr;
}

void TrackingThread::run()
{
    Logger::info("[TrackingThread] 시작");

    try
    {
        DetectionResult detections;
        while (input_queue_.pop(detections))
        {
            const auto queue_started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};

            if (!detections.frame ||
                detections.model_generation != runtime_control_.generation.load())
                continue;

            auto tracker = tracker_manager_.snapshot();
            if (!tracker.tracker)
                throw std::runtime_error("Active tracker is missing");

            if (tracker_epoch_ != tracker.epoch)
            {
                // 새 인스턴스는 이미 초기 상태이므로 모델 세대만 기록한다.
                tracker_epoch_ = tracker.epoch;
                tracker_generation_ = detections.model_generation;
            }
            else if (tracker_generation_ != detections.model_generation)
            {
                tracker.tracker->reset();
                tracker_generation_ = detections.model_generation;
            }

            auto tracks = std::make_shared<TrackingResult>();
            tracks->frame = detections.frame;
            tracks->model_generation = detections.model_generation;
            tracks->tracker_epoch = tracker.epoch;

            const auto started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};
            tracks->tracks = tracker.tracker->track(detections.detections);
            const auto finished_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};

            // 교체 중 이전 인스턴스가 마친 프레임은 새 트래커 결과로 게시하지 않는다.
            if (tracker_manager_.active_epoch() != tracker.epoch ||
                detections.model_generation != runtime_control_.generation.load())
                continue;

            if (measurement_enabled_)
            {
                // Tracking 완료 시점에서 Camera frame 수신 이후 지연을 기록한다.
                if (measurements_)
                    measurements_->record(detections.model_generation, MetricStage::Tracking,
                        make_stage_metric(detections.frame->metadata, detections.enqueued_at,
                            queue_started_at, started_at, finished_at, true));
                else record_stage_metric(metrics_, detections.frame->metadata,
                    detections.enqueued_at, queue_started_at,
                    started_at, finished_at, true);
            }

            bool control_push_ok = true;
            const bool published = tracker_manager_.publish_if_current(
                tracker.epoch, [&] {
                    TrackingResultPtr shared_result = tracks;
                    // 제어는 최신 결과 하나, GUI는 최신 두 결과만 유지한다.
                    if (measurement_enabled_)
                        tracks->enqueued_at = std::chrono::steady_clock::now();
                    control_push_ok = control_output_queue_.push_latest(shared_result, 1);
                    if (control_push_ok)
                        gui_output_queue_.push_latest(std::move(shared_result), 2);
                });
            if (!published) continue;
            if (!control_push_ok)
                throw std::runtime_error("Tracking control result queue is closed");
        }
    }
    catch (const std::exception& e)
    {
        error_message_ = e.what();
        failed_.store(true);
        Logger::error("[TrackingThread] " + error_message_);
        // 입력 큐는 탐지 생산자가 콜백을 합류시킨 뒤 닫는다.
        runtime_control_.request_stop();
    }
    catch (...)
    {
        error_message_ = "알 수 없는 치명적 오류";
        failed_.store(true);
        Logger::error("[TrackingThread] " + error_message_);
        // 입력 큐는 탐지 생산자가 콜백을 합류시킨 뒤 닫는다.
        runtime_control_.request_stop();
    }

    control_output_queue_.close();
    gui_output_queue_.close();
    Logger::info("[TrackingThread] 종료");
}
