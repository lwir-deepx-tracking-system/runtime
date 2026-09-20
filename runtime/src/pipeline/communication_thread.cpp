#include "pipeline/communication_thread.hpp"

#include "common/logger.hpp"

#include <chrono>

CommunicationThread::CommunicationThread(
    STM32Link& link,
    ThreadSafeQueue<TargetSelection>& input_queue,
    bool measurement_enabled)
    : link_(link), input_queue_(input_queue),
      measurement_enabled_(measurement_enabled)
{
}

void CommunicationThread::start()
{
    pthread_create(&thread_, nullptr, &CommunicationThread::thread_func, this);
}

void CommunicationThread::join()
{
    pthread_join(thread_, nullptr);
}

void* CommunicationThread::thread_func(void* arg)
{
    static_cast<CommunicationThread*>(arg)->run();
    return nullptr;
}

void CommunicationThread::run()
{
    Logger::info("[CommunicationThread] 시작");

    TargetSelection selection;

    // 선택 결과를 통신 경계에 전달한다.
    while (input_queue_.pop(selection))
    {
        const auto started_at = measurement_enabled_ ?
            std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        link_.send(selection);
        if (measurement_enabled_)
            // 현재 send()는 빈 구현이므로 전송 지연이 아닌 함수 반환 시각이다.
            record_stage_metric(metrics_, selection.metadata, started_at,
                std::chrono::steady_clock::now(), true);
    }

    Logger::info("[CommunicationThread] 종료");
}
