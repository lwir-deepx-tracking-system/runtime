#include "pipeline/detection_thread.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

#include "common/logger.hpp"

DetectionThread::DetectionThread(
    RuntimeControl& runtime_control,
    ActivePipelineSlot& active_pipeline,
    ThreadSafeQueue<FrameMessage>& input_queue,
    ThreadSafeQueue<DetectionResult>& output_queue,
    ThreadSafeQueue<TrackingResultPtr>& gui_output_queue,
    bool gui_enabled,
    bool measurement_enabled, IMeasurementSink* measurements)
    : runtime_control_(runtime_control),
      active_pipeline_(active_pipeline),
      input_queue_(input_queue),
      output_queue_(output_queue),
      gui_output_queue_(gui_output_queue),
      gui_enabled_(gui_enabled),
      measurement_enabled_(measurement_enabled), measurements_(measurements)
{
}

void DetectionThread::start()
{
    if (started_) return;
    thread_ = std::thread(&DetectionThread::thread_func, this);
    started_ = true;
}

void DetectionThread::join()
{
    if (!started_) return;
    if(thread_.joinable())thread_.join();
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
    Logger::info("[DetectionThread] 시작");
    auto last_bypass_sent = std::chrono::steady_clock::time_point{};
    bool has_sent_bypass = false;
    std::uint64_t observed_interrupt = 0;
    // 활성 슬롯이 모델 매니저로 이동해도 자신이 제출한 파이프라인을 기다릴 수 있다.
    std::shared_ptr<IDetectionPipeline> submitted_pipeline;
    // 정지 확인은 콜백 반환과 탐지 스레드의 참조 해제가 모두 끝난 뒤에만 게시한다.
    const auto quiesce = [&]
    {
        if(submitted_pipeline)
        {
            submitted_pipeline->wait_idle();
            submitted_pipeline.reset();
        }
        runtime_control_.set_detection_quiesced(true);
    };

    try
    {
        FrameMessage input;
        while (!failed_.load())
        {
            const auto popped = input_queue_.pop_interruptible(input, observed_interrupt);
            if (popped == ThreadSafeQueue<FrameMessage>::PopResult::Closed)
                break;
            if (popped == ThreadSafeQueue<FrameMessage>::PopResult::Interrupted)
            {
                if (runtime_control_.state.load() == RuntimeState::Stopping)
                    break;
                if (runtime_control_.state.load() != RuntimeState::Running)
                    quiesce();
                continue;
            }
            if (!input.frame)
                continue;

            const RuntimeState state = runtime_control_.state.load();
            if (state == RuntimeState::Stopping)
                break;

            if (state != RuntimeState::Running)
            {
                quiesce();

                const auto now = std::chrono::steady_clock::now();
                if (gui_enabled_ && (!has_sent_bypass ||
                    now - last_bypass_sent >= std::chrono::milliseconds(33)))
                {
                    auto bypass = std::make_shared<TrackingResult>();
                    bypass->frame = input.frame;
                    bypass->model_generation = runtime_control_.generation.load();  // 어떤 추론 모델인지 모델 인덱스 전달
                    bypass->bypass = true;
                    gui_output_queue_.push_latest(std::move(bypass), 2);
                    last_bypass_sent = now;
                    has_sent_bypass = true;
                }
                continue;
            }

            runtime_control_.set_detection_quiesced(false);
            std::shared_ptr<IDetectionPipeline> pipeline =
                active_pipeline_.snapshot();
            if (!pipeline)
            {
                runtime_control_.state.store(RuntimeState::SwitchFailed);
                quiesce();
                continue;
            }
            submitted_pipeline = pipeline;

            const auto queue_started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};
            const auto submitted_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};
            auto frame = input.frame;
            const auto enqueued_at = input.enqueued_at;
            const std::uint64_t sequence = next_submit_sequence_++;
            const std::uint64_t generation = runtime_control_.generation.load();
            if (measurements_ && frame->metadata.capture_processing_ms) {
                StageMetric camera_metric;
                camera_metric.frame_id = frame->metadata.frame_id;
                camera_metric.processing_ms = *frame->metadata.capture_processing_ms;
                measurements_->record(generation, MetricStage::Camera, camera_metric);
            }

            pipeline->submit_async(
                frame,
                [this, frame, enqueued_at, sequence, generation,
                    queue_started_at, submitted_at]
                    (std::vector<Detection> detections,
                     std::exception_ptr error,
                     DetectionTiming detection_timing) mutable
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

                    if (detection_timing.frame_id != frame->metadata.frame_id)
                    {
                        set_error("Detection timing frame ID mismatch");
                        input_queue_.close();
                        return;
                    }

                    DetectionResult result;
                    result.frame = frame;
                    result.detections = std::move(detections);
                    result.model_generation = generation;

                    if (measurement_enabled_)
                    {
                        if (measurements_) {
                            measurements_->record(generation, MetricStage::Detection,
                                make_stage_metric(frame->metadata, enqueued_at,
                                    queue_started_at, submitted_at, completed_at));
                            measurements_->record_detection(generation, detection_timing);
                        } else {
                            std::lock_guard<std::mutex> lock(metrics_mutex_);
                            record_stage_metric(
                            metrics_, frame->metadata, enqueued_at,
                            queue_started_at, submitted_at, completed_at);
                            if (detection_timings_.size() < kMaxStageMetrics)
                                detection_timings_.push_back(detection_timing);
                        }
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
        }   // end of while(!failed_.load() && input_queue_.pop(input))
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
        quiesce();
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
    Logger::info("[DetectionThread] 종료");
}
