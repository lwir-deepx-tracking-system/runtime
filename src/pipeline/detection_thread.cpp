#include "pipeline/detection_thread.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

#include "common/logger.hpp"

DetectionThread::DetectionThread(
    IDetectionPipeline& pipeline,
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
    if (started_) return;
    const int result = pthread_create(
        &thread_, nullptr, &DetectionThread::thread_func, this);
    if (result != 0)
        throw std::runtime_error("DetectionThread start failed");
    started_ = true;
}

void DetectionThread::join()
{
    if (!started_) return;
    pthread_join(thread_, nullptr);
    started_ = false;
}

void* DetectionThread::thread_func(void* arg)
{
    static_cast<DetectionThread*>(arg)->run();
    return nullptr;
}

void DetectionThread::set_error(const std::string& message)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (error_message_.empty())
            error_message_ = message;
    }
    failed_.store(true);
}

void DetectionThread::run()
{
    Logger::info("[DetectionThread] started");

    try
    {
        FrameMessage input;
        while (!failed_.load() && input_queue_.pop(input))
        {
            const auto queue_started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};
            if (!input.frame)
                continue;

            const auto submitted_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};
            auto frame = input.frame;
            const auto enqueued_at = input.enqueued_at;
            const std::uint64_t sequence = next_submit_sequence_++;

            pipeline_.submit_async(
                frame,
                [this, frame = std::move(frame), enqueued_at, sequence,
                 queue_started_at, submitted_at]
                (std::vector<Detection> detections, std::exception_ptr error) mutable
                {
                    const auto completed_at = measurement_enabled_ ?
                        std::chrono::steady_clock::now() :
                        std::chrono::steady_clock::time_point{};

                    if (error)
                    {
                        try
                        {
                            std::rethrow_exception(error);
                        }
                        catch (const std::exception& e)
                        {
                            set_error(e.what());
                        }
                        catch (...)
                        {
                            set_error("Unknown detection callback error");
                        }
                        input_queue_.close();
                        return;
                    }

                    DetectionResult result;
                    result.frame = frame;
                    result.detections = std::move(detections);

                    if (measurement_enabled_)
                    {
                        std::lock_guard<std::mutex> lock(metrics_mutex_);
                        record_stage_metric(
                            metrics_, frame->metadata, enqueued_at,
                            queue_started_at, submitted_at, completed_at);
                        result.enqueued_at = completed_at;
                    }

                    std::lock_guard<std::mutex> lock(reorder_mutex_);
                    completed_results_.emplace(sequence, std::move(result));
                    for (;;)
                    {
                        auto ready = completed_results_.find(next_output_sequence_);
                        if (ready == completed_results_.end())
                            break;
                        if (!output_queue_.push(std::move(ready->second)))
                        {
                            set_error("Detection result queue is closed");
                            break;
                        }
                        completed_results_.erase(ready);
                        ++next_output_sequence_;
                    }
                });
        }
    }
    catch (const std::exception& e)
    {
        set_error(e.what());
        input_queue_.close();
    }
    catch (...)
    {
        set_error("Unknown fatal detection error");
        input_queue_.close();
    }

    try
    {
        pipeline_.wait_idle();
    }
    catch (const std::exception& e)
    {
        set_error(e.what());
    }
    catch (...)
    {
        set_error("Unknown error while draining inference requests");
    }

    output_queue_.close();
    Logger::info("[DetectionThread] stopped");
}
