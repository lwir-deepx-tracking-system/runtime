#include "pipeline/detection_thread.hpp"

#include <chrono>
#include <exception>
#include <string>
#include <utility>

#include "common/logger.hpp"

DetectionThread::DetectionThread(
    DetectionPipeline& pipeline,
    ThreadSafeQueue<FrameMessage>& input_queue,
    ThreadSafeQueue<DetectionResult>& output_queue,
    bool measurement_enabled)
    : pipeline_(pipeline),
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

    try
    {
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
            result.detections = pipeline_.detect(*input.frame);

            if (measurement_enabled_)
            {
                const auto finished_at = std::chrono::steady_clock::now();
                record_stage_metric(
                    metrics_, input.frame->metadata, input.enqueued_at,
                    started_at, finished_at);
                result.enqueued_at = finished_at;
            }

            if (!output_queue_.push(std::move(result)))
                break;
        }
    }
    catch (const std::exception& e)
    {
        Logger::error(std::string("[DetectionThread] ") + e.what());
    }

    output_queue_.close();
    Logger::info("[DetectionThread] 종료");
}
