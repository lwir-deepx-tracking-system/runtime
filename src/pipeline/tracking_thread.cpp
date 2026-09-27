#include "pipeline/tracking_thread.hpp"

#include <chrono>
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
    pthread_create(&thread_, nullptr, &TrackingThread::thread_func, this);
}

void TrackingThread::join()
{
    pthread_join(thread_, nullptr);
}

void* TrackingThread::thread_func(void* arg)
{
    static_cast<TrackingThread*>(arg)->run();
    return nullptr;
}

void TrackingThread::run()
{
    Logger::info("[TrackingThread] 시작");

    DetectionResult detections;

    while (input_queue_.pop(detections))
    {
        if (!detections.frame)
            continue;

        const auto started_at = measurement_enabled_ ?
            std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

        auto tracks = std::make_shared<TrackingResult>();
        tracks->frame = detections.frame;
        tracks->tracks = tracker_.track(detections.detections);

        if (measurement_enabled_)
        {
            // 추적 완료 지점에서 카메라 획득 이후 전체 지연을 기록한다.
            record_stage_metric(metrics_, detections.frame->metadata,
                detections.enqueued_at, started_at,
                std::chrono::steady_clock::now(), true);
            tracks->enqueued_at = std::chrono::steady_clock::now();
        }

        TrackingResultPtr shared_result = std::move(tracks);

        // 제어 경로는 모든 결과를 받고, GUI는 오래된 화면이 쌓이지 않게
        // 최신 두 결과만 유지한다.
        if (!control_output_queue_.push(shared_result))
            break;
        gui_output_queue_.push_latest(std::move(shared_result), 2);
    }

    control_output_queue_.close();
    gui_output_queue_.close();
    Logger::info("[TrackingThread] 종료");
}
