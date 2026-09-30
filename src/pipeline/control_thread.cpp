#include "pipeline/control_thread.hpp"

#include "common/logger.hpp"

#include <chrono>

// Tracking 결과 queue와 선택 ID 상태를 실제 Orange Pi 제어 경계에 연결한다.
ControlThread::ControlThread(
    GimbalController& controller,
    TargetSelector& target_selector,
    ThreadSafeQueue<TrackingResultPtr>& input_queue,
    bool control_enabled,
    bool measurement_enabled)
    : controller_(controller),
      target_selector_(target_selector),
      input_queue_(input_queue),
      control_enabled_(control_enabled),
      measurement_enabled_(measurement_enabled)
{
}

// 제어 소비자를 producer보다 먼저 대기시킬 수 있도록 독립 pthread로 시작한다.
void ControlThread::start()
{
    pthread_create(&thread_, nullptr, &ControlThread::thread_func, this);
}

// TrackingThread가 queue를 닫고 남은 결과를 모두 소비할 때까지 기다린다.
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
        Logger::info("[ControlThread] 제어 비활성화: Tracking 결과만 소비합니다");

    TrackingResultPtr result;

    while (input_queue_.pop(result))
    {
        if (!result || !result->frame)
            continue;
        if (!control_enabled_)
            continue;

        const auto started_at = measurement_enabled_ ?
            std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};

        const int selected_id = target_selector_.get_selected_id();
        const Track* selected_track = nullptr;
        if (selected_id >= 0)
        {
            for (const Track& track : result->tracks)
            {
                if (track.track_id == selected_id)
                {
                    selected_track = &track;
                    break;
                }
            }
        }

        controller_.apply(selected_track, *result->frame);

        if (measurement_enabled_)
        {
            // Tracking queue 대기와 Camera 획득 이후 전체 제어 지연을 기록한다.
            record_stage_metric(metrics_, result->frame->metadata,
                result->enqueued_at, started_at,
                std::chrono::steady_clock::now(), true);
        }
    }

    Logger::info("[ControlThread] 종료");
}
