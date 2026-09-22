#include "pipeline/tracking_thread.hpp"

#include <chrono>
#include <utility>

#include "common/logger.hpp"

// Tracker와 입출력 queue를 Tracking worker에 연결한다.
TrackingThread::TrackingThread(
    Tracker& tracker,
    ThreadSafeQueue<DetectionResult>& input_queue,
    ThreadSafeQueue<TrackingResult>& output_queue,
    bool measurement_enabled)
    : tracker_(tracker),
      input_queue_(input_queue),
      output_queue_(output_queue),
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
        const auto started_at = measurement_enabled_ ?
            std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

        TrackingResult tracks;
        tracks.tracks = tracker_.track(detections.detections);
        tracks.metadata = detections.metadata;

        if (measurement_enabled_)
        {
            // 추적 완료 지점에서 카메라 획득 이후 전체 지연을 기록한다.
            record_stage_metric(metrics_, detections.metadata, started_at,
                std::chrono::steady_clock::now(), true);
            tracks.metadata.enqueued_at = std::chrono::steady_clock::now();
        }

        if (!output_queue_.push(std::move(tracks)))
            break;
    }

    // 마지막 결과까지 소비한 후 다음 stage를 종료한다.
    output_queue_.close();
    Logger::info("[TrackingThread] 종료");
}
