#include "pipeline/gui_sender_thread.hpp"

#include <stdexcept>

#include "common/logger.hpp"

GuiSenderThread::GuiSenderThread(
    GuiSender& sender,
    ThreadSafeQueue<TrackingResultPtr>& input_queue)
    : sender_(sender), input_queue_(input_queue)
{
}

void GuiSenderThread::start()
{
    if (started_) return;
    const int result = pthread_create(
        &thread_, nullptr, &GuiSenderThread::thread_func, this);
    if (result != 0)
        throw std::runtime_error("GuiSenderThread 생성 실패");
    started_ = true;
}

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

void GuiSenderThread::run()
{
    Logger::info("[GuiSenderThread] 시작");
    TrackingResultPtr result;
    while (input_queue_.pop(result))
    {
        if (!result || !result->frame) continue;
        if (!sender_.send(*result))
        {
            Logger::error("[GuiSenderThread] GUI 전송 실패");
            break;
        }
    }
    sender_.stop();
    Logger::info("[GuiSenderThread] 종료");
}
