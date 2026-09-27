#include "pipeline/detection_thread.hpp"

#include <chrono>
#include <utility>

#include "common/logger.hpp"

DetectionThread::DetectionThread(
    Detector& detector,
    ThreadSafeQueue<FrameMessage>& input_queue,
    ThreadSafeQueue<DetectionResult>& output_queue,
    bool measurement_enabled)
    : detector_(detector),
      input_queue_(input_queue),
      output_queue_(output_queue),
      measurement_enabled_(measurement_enabled)
{
}

void DetectionThread::start()
{
    pthread_create(&thread_, nullptr, &DetectionThread::thread_func, this);
}

void DetectionThread::join()
{
    pthread_join(thread_, nullptr);
}

void* DetectionThread::thread_func(void* arg)
{
    static_cast<DetectionThread*>(arg)->run();
    return nullptr;
}

void DetectionThread::run()
{
    Logger::info("[DetectionThread] 시작");

    FrameMessage input;
    while (input_queue_.pop(input))
    {
        if (!input.frame)
            continue;

        const auto started_at = measurement_enabled_ ?
            std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};

        DetectionResult result;
        result.frame = input.frame;
        result.detections = detector_.detect(*input.frame);

        if (measurement_enabled_)
        {
            record_stage_metric(
                metrics_, input.frame->metadata, input.enqueued_at, started_at,
                std::chrono::steady_clock::now());
            result.enqueued_at = std::chrono::steady_clock::now();
        }

        if (!output_queue_.push(std::move(result)))
            break;
    }

    output_queue_.close();
    Logger::info("[DetectionThread] 종료");
}
