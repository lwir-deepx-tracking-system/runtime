#include "pipeline/gui_receiver_thread.hpp"

#include <stdexcept>

#include "common/logger.hpp"

GuiReceiverThread::GuiReceiverThread(
    GuiReceiver& receiver,
    TargetSelector& target_selector)
    : receiver_(receiver), target_selector_(target_selector)
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

// 검증된 SelectTrack command를 TargetSelector의 공유 선택 ID에 반영한다.
void GuiReceiverThread::run()
{
    Logger::info("[GuiReceiverThread] 시작");
    GuiCommand command;

    // GuiReceiver가 network와 protocol 검증을 끝낸 command만 반환하므로,
    // 여기서는 SelectTrack의 ID를 thread-safe 상태에 반영하는 일만 한다.
    while (receiver_.receive(command))
    {
        if (command.type == GuiCommandType::SelectTrack)
            target_selector_.set_selected_id(command.track_id);
    }
    Logger::info("[GuiReceiverThread] 종료");
}
