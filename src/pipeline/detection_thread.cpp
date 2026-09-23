#include "pipeline/detection_thread.hpp"

#include <chrono>
#include <exception>
#include <string>
#include <utility>
#include "common/logger.hpp"

DetectionThread::DetectionThread(DetectionPipeline& pipeline, ThreadSafeQueue<Frame>& input,
                                 ThreadSafeQueue<DetectionResult>& output, bool measurement_enabled)
    : pipeline_(pipeline), input_queue_(input), output_queue_(output),
      measurement_enabled_(measurement_enabled) {}

void DetectionThread::start() { pthread_create(&thread_, nullptr, &DetectionThread::thread_func, this); }
void DetectionThread::join() { pthread_join(thread_, nullptr); }
void* DetectionThread::thread_func(void* argument)
{
    static_cast<DetectionThread*>(argument)->run();
    return nullptr;
}

void DetectionThread::run()
{
    Logger::info("[DetectionThread] start");
    try {
        Frame frame;
        while (input_queue_.pop(frame)) {
            const auto started = measurement_enabled_ ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
            DetectionResult result = pipeline_.run(frame);
            result.metadata = frame.metadata;
            if (measurement_enabled_) {
                const auto finished = std::chrono::steady_clock::now();
                record_stage_metric(metrics_, frame.metadata, started, finished, true);
                result.metadata.enqueued_at = finished;
            }
            if (!output_queue_.push(std::move(result))) break;
        }
    } catch (const std::exception& e) {
        Logger::error(std::string("[DetectionThread] ") + e.what());
    }
    output_queue_.close();
    Logger::info("[DetectionThread] stop");
}
