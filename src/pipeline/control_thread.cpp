#include "pipeline/control_thread.hpp"

#include "common/logger.hpp"

#include <chrono>

ControlThread::ControlThread(
    GimbalController& controller,
    ThreadSafeQueue<TargetSelection>& input_queue,
    bool control_enabled,
    bool measurement_enabled)
    : controller_(controller), input_queue_(input_queue),
      control_enabled_(control_enabled),
      measurement_enabled_(measurement_enabled)
{
}

void ControlThread::start()
{
    pthread_create(&thread_, nullptr, &ControlThread::thread_func, this);
}

void ControlThread::join()
{
    pthread_join(thread_, nullptr);
}

void* ControlThread::thread_func(void* arg)
{
    static_cast<ControlThread*>(arg)->run();
    return nullptr;
}

void ControlThread::run()
{
    Logger::info("[ControlThread] 시작");

    if (!control_enabled_)
        Logger::info("[ControlThread] 제어 비활성화: 선택 결과만 소비합니다");

    TargetSelection selection;

    while (input_queue_.pop(selection))
    {
        if (control_enabled_)
        {
            const auto started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            controller_.apply(selection);
            if (measurement_enabled_)
                record_stage_metric(metrics_, selection.metadata, started_at,
                    std::chrono::steady_clock::now(), true);
        }
    }

    Logger::info("[ControlThread] 종료");
}
