#include "pipeline/gui_receiver_thread.hpp"

#include <exception>
#include <stdexcept>

#include "common/logger.hpp"

GuiReceiverThread::GuiReceiverThread(GuiReceiver& receiver)
    : receiver_(receiver)
{
}

// TCP command 수신 pthread를 만들고 중복 start를 막는다.
void GuiReceiverThread::start()
{
    if (started_) return;
    const int result = pthread_create(
        &thread_, nullptr, &GuiReceiverThread::thread_func, this);
    if (result != 0)
        throw std::runtime_error("GuiReceiverThread 생성 실패");
    started_ = true;
}

// socket I/O를 소유한 GuiReceiver에 종료를 요청해 receive loop를 깨운다.
void GuiReceiverThread::stop()
{
    receiver_.stop();
}

// receive loop가 끝날 때까지 기다린 뒤 다시 join 가능한 상태로 되돌린다.
void GuiReceiverThread::join()
{
    if (!started_) return;
    pthread_join(thread_, nullptr);
    started_ = false;
}

void* GuiReceiverThread::thread_func(void* arg)
{
    static_cast<GuiReceiverThread*>(arg)->run();
    return nullptr;
}

// PC 통신 session을 처리한다. 입력 ID 기반 추적은 여기서 수행하지 않는다.
void GuiReceiverThread::run()
{
    Logger::info("[GuiReceiverThread] 시작");
    GuiCommand command;

    try
    {
        // The receiver delegates commands to its configured handler strategy.
        while (receiver_.receive(command)) {}

        if (!receiver_.stop_requested())
        {
            error_message_ = "GUI 명령 수신이 예기치 않게 종료되었습니다";
            failed_.store(true);
            Logger::error("[GuiReceiverThread] " + error_message_);
        }
    }
    catch (const std::exception& e)
    {
        error_message_ = e.what();
        failed_.store(true);
        Logger::error("[GuiReceiverThread] " + error_message_);
    }
    catch (...)
    {
        error_message_ = "알 수 없는 치명적 오류";
        failed_.store(true);
        Logger::error("[GuiReceiverThread] " + error_message_);
    }
    Logger::info("[GuiReceiverThread] 종료");
}
