#include "pipeline/gui_sender_thread.hpp"

#include <stdexcept>

#include "common/logger.hpp"

GuiSenderThread::GuiSenderThread(
    GuiSender& sender,
    ThreadSafeQueue<TrackingResultPtr>& input_queue)
    : sender_(sender), input_queue_(input_queue)
{
}

// queue consumer pthread를 만들고 중복 start를 막는다.
void GuiSenderThread::start()
{
    if (started_) return;
    const int result = pthread_create(
        &thread_, nullptr, &GuiSenderThread::thread_func, this);
    if (result != 0)
        throw std::runtime_error("GuiSenderThread 생성 실패");
    started_ = true;
}

// queue 종료 또는 전송 실패로 run()이 끝날 때까지 기다린다.
void GuiSenderThread::join()
{
    if (!started_) return;
    pthread_join(thread_, nullptr);
    started_ = false;
}

void* GuiSenderThread::thread_func(void* arg)
{
    static_cast<GuiSenderThread*>(arg)->run();
    return nullptr;
}

// TrackingResultPtr을 queue에서 꺼내 실제 송신 경계인 GuiSender에 순서대로 넘긴다.
void GuiSenderThread::run()
{
    Logger::info("[GuiSenderThread] 시작");
    TrackingResultPtr result;

    // TrackingThread가 닫을 때까지 queue를 소비한다. shared_ptr에 함께 담긴
    // frame과 tracks는 같은 TrackingResult에서 생성된 동일 frame의 데이터이다.
    while (input_queue_.pop(result))
    {
        if (!result || !result->frame) continue;
        if (!sender_.send(*result))
        {
            Logger::error("[GuiSenderThread] GUI 전송 실패");
            break;
        }
    }
    // 정상 queue 종료와 전송 오류 모두 같은 경로로 송신 자원을 정리한다.
    sender_.stop();
    Logger::info("[GuiSenderThread] 종료");
}
