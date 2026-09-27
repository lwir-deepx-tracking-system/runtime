#include "pipeline/control_thread.hpp"

#include "common/logger.hpp"

#include <chrono>

// 선택 결과 queue와 실제 Orange Pi 제어 경계를 worker에 연결한다.
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

// 제어 소비자를 producer보다 먼저 대기시킬 수 있도록 독립 pthread로 시작한다.
void ControlThread::start()
{
    pthread_create(&thread_, nullptr, &ControlThread::thread_func, this);
}

// upstream이 queue를 닫고 남은 선택 결과를 모두 소비할 때까지 기다린다.
void ControlThread::join()
{
    pthread_join(thread_, nullptr);
}

// pthread 진입점을 객체의 run() 메서드로 전달한다.
void* ControlThread::thread_func(void* arg)
{
    static_cast<ControlThread*>(arg)->run();
    return nullptr;
}

void ControlThread::run()
{
    Logger::info("[ControlThread] 시작");

    // 제어가 꺼져 있어도 queue를 계속 비워 전체 pipeline의 정상 종료를 보장한다.
    if (!control_enabled_)
        Logger::info("[ControlThread] 제어 비활성화: 선택 결과만 소비합니다");

    TargetSelection selection;

    while (input_queue_.pop(selection))
    {
        if (control_enabled_)
        {
            // Frame이 없으면 지연 측정 기준도 없으므로 하드웨어 호출을 건너뛴다.
            if (!selection.frame)
                continue;

            const auto started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            controller_.apply(selection);
            if (measurement_enabled_)
                // Control e2e는 Camera 획득부터 제어 함수 반환까지의 시간이다.
                record_stage_metric(metrics_, selection.frame->metadata,
                    selection.enqueued_at, started_at,
                    std::chrono::steady_clock::now(), true);
        }
    }

    Logger::info("[ControlThread] 종료");
}
