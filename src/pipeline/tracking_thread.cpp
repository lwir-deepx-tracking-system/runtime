#include "pipeline/tracking_thread.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

#include "common/logger.hpp"

// Tracker와 입출력 queue를 Tracking worker에 연결한다.
TrackingThread::TrackingThread(
    Tracker& tracker,
    ThreadSafeQueue<DetectionResult>& input_queue,
    ThreadSafeQueue<TrackingResultPtr>& control_output_queue,
    ThreadSafeQueue<TrackingResultPtr>& gui_output_queue,
    bool measurement_enabled)
    : tracker_(tracker),
      input_queue_(input_queue),
      control_output_queue_(control_output_queue),
      gui_output_queue_(gui_output_queue),
      measurement_enabled_(measurement_enabled)
{
}

void TrackingThread::start()
{
    if (started_) return;
    const int result = pthread_create(
        &thread_, nullptr, &TrackingThread::thread_func, this);
    if (result != 0)
        throw std::runtime_error("TrackingThread 생성 실패");
    started_ = true;
}

void TrackingThread::join()
{
    if (!started_) return;
    pthread_join(thread_, nullptr);
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

            if (!detections.frame)
                continue;

            auto tracks = std::make_shared<TrackingResult>();
            tracks->frame = detections.frame;

            const auto started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};
            tracks->tracks = tracker_.track(detections.detections);
            const auto finished_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};

            if (measurement_enabled_)
            {
                // Tracking 완료 시점에서 Camera frame 수신 이후 지연을 기록한다.
                record_stage_metric(metrics_, detections.frame->metadata,
                    detections.enqueued_at, queue_started_at,
                    started_at, finished_at, true);
            }

            TrackingResultPtr shared_result = tracks;

            // 제어 경로는 모든 결과를 받고, GUI는 오래된 화면이 쌓이지 않게
            // 최신 두 결과만 유지한다.
            if (measurement_enabled_)
                tracks->enqueued_at = std::chrono::steady_clock::now();
            if (!control_output_queue_.push(shared_result))
                break;
            gui_output_queue_.push_latest(std::move(shared_result), 2);
        }
    }
    catch (const std::exception& e)
    {
        error_message_ = e.what();
        failed_.store(true);
        Logger::error("[TrackingThread] " + error_message_);
        input_queue_.close();
    }
    catch (...)
    {
        error_message_ = "알 수 없는 치명적 오류";
        failed_.store(true);
        Logger::error("[TrackingThread] " + error_message_);
        input_queue_.close();
    }

    control_output_queue_.close();
    gui_output_queue_.close();
    Logger::info("[TrackingThread] 종료");
}
