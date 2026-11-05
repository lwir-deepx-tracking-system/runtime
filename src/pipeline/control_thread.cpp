#include "pipeline/control_thread.hpp"

#include "common/logger.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>

// Tracking 결과를 장치 실행 스레드의 최신 결과 mailbox에 전달한다.
ControlThread::ControlThread(
    ThreadSafeQueue<TrackingResultPtr>& input_queue,
    GimbalCommandExecutor* executor,
    bool measurement_enabled, IMeasurementSink* measurements)
    : executor_(executor),
      input_queue_(input_queue),
      measurement_enabled_(measurement_enabled), measurements_(measurements)
{
}

// 제어 소비자를 producer보다 먼저 대기시킬 수 있도록 독립 thread로 시작한다.
void ControlThread::start()
{
    if (started_) return;
    thread_ = std::thread(&ControlThread::thread_func, this);
    started_ = true;
}

// TrackingThread가 queue를 닫고 남은 결과를 모두 소비할 때까지 기다린다.
void ControlThread::join()
{
    if (!started_) return;
    if(thread_.joinable()) thread_.join();
    started_ = false;
}

// thread 진입점을 객체의 run() 메서드로 전달한다.
void* ControlThread::thread_func(void* arg)
{
    static_cast<ControlThread*>(arg)->run();
    return nullptr;
}

void ControlThread::run()
{
    Logger::info("[ControlThread] 시작");

    // 제어가 꺼져 있어도 queue를 계속 비워 전체 pipeline의 정상 종료를 보장한다.
    if (!executor_)
        Logger::info("[ControlThread] AUTO 제어 비활성화: Tracking 결과만 소비합니다");

    try
    {
        TrackingResultPtr result;
        while (input_queue_.pop(result))
        {
            const auto queue_started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};

            if (!result || !result->frame)
                continue;
            if (!executor_)
                continue;

            const auto started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};

            executor_->publish(result);
            const auto finished_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};

            if (measurement_enabled_)
            {
                // mailbox 제출까지의 지연이다. 실제 제어/장치 완료 시간이 아니다.
                if (measurements_)
                    measurements_->record(result->model_generation, MetricStage::Control,
                        make_stage_metric(result->frame->metadata, result->enqueued_at,
                            queue_started_at, started_at, finished_at, true));
                else record_stage_metric(metrics_, result->frame->metadata,
                    result->enqueued_at, queue_started_at,
                    started_at, finished_at, true);
            }
        }
    }
    catch (const std::exception& e)
    {
        error_message_ = e.what();
        failed_.store(true);
        Logger::error("[ControlThread] " + error_message_);
        input_queue_.close();
    }
    catch (...)
    {
        error_message_ = "알 수 없는 치명적 오류";
        failed_.store(true);
        Logger::error("[ControlThread] " + error_message_);
        input_queue_.close();
    }

    if (executor_) executor_->shutdown();
    Logger::info("[ControlThread] 종료");
}
