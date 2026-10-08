#include "pipeline/gui_sender_thread.hpp"

#include <chrono>
#include <cstdint>
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
    try
    {
        while (input_queue_.pop(result))
        {
            if (!result || !result->frame) continue;

            // PC와 비교할 수 있도록 GUI 송신 처리를 시작한 epoch microsecond를 보낸다.
            const auto gui_started_at = std::chrono::system_clock::now();
            const std::uint64_t gui_started_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    gui_started_at.time_since_epoch()).count());

            if (!sender_.send(*result, gui_started_us))
            {
                error_message_ = "GUI 전송 실패";
                failed_.store(true);
                Logger::error("[GuiSenderThread] " + error_message_);
                input_queue_.close();
                break;
            }
        }
    }
    catch (const std::exception& e)
    {
        error_message_ = e.what();
        failed_.store(true);
        input_queue_.close();
        Logger::error("[GuiSenderThread] " + error_message_);
    }
    catch (...)
    {
        error_message_ = "알 수 없는 치명적 오류";
        failed_.store(true);
        input_queue_.close();
        Logger::error("[GuiSenderThread] " + error_message_);
    }
    // 정상 queue 종료와 전송 오류 모두 같은 경로로 송신 자원을 정리한다.
    sender_.stop();
    Logger::info("[GuiSenderThread] 종료");
}
