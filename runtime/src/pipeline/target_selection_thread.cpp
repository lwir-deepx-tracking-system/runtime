#include "pipeline/target_selection_thread.hpp"

#include "common/logger.hpp"

#include <chrono>
#include <utility>

TargetSelectionThread::TargetSelectionThread(
    TargetSelector& selector,
    ThreadSafeQueue<TrackingResult>& input_queue,
    ThreadSafeQueue<TargetSelection>& output_queue,
    bool measurement_enabled)
    : selector_(selector),
      input_queue_(input_queue),
      output_queue_(output_queue),
      measurement_enabled_(measurement_enabled)
{
}

void TargetSelectionThread::start()
{
    pthread_create(&thread_, nullptr, &TargetSelectionThread::thread_func, this);
}

void TargetSelectionThread::join()
{
    pthread_join(thread_, nullptr);
}

void* TargetSelectionThread::thread_func(void* arg)
{
    static_cast<TargetSelectionThread*>(arg)->run();
    return nullptr;
}

void TargetSelectionThread::run()
{
    Logger::info("[TargetSelectionThread] 시작");

    TrackingResult tracks;

    // Tracking 결과마다 선택 ID의 관측 여부를 다음 queue로 보낸다.
    while (input_queue_.pop(tracks))
    {
        const auto started_at = measurement_enabled_ ?
            std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        TargetSelection selection = selector_.select(tracks.tracks);
        selection.metadata = tracks.metadata;
        if (measurement_enabled_)
        {
            record_stage_metric(metrics_, tracks.metadata, started_at,
                std::chrono::steady_clock::now());
            selection.metadata.enqueued_at = std::chrono::steady_clock::now();
        }
        if (!output_queue_.push(std::move(selection)))
            break;
    }

    output_queue_.close();
    Logger::info("[TargetSelectionThread] 종료");
}
