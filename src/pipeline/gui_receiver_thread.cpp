#include "pipeline/gui_receiver_thread.hpp"

#include <stdexcept>

#include "common/logger.hpp"

GuiReceiverThread::GuiReceiverThread(
    GuiReceiver& receiver,
    TargetSelector& target_selector)
    : receiver_(receiver), target_selector_(target_selector)
{
}

void GuiReceiverThread::start()
{
    if (started_) return;
    const int result = pthread_create(
        &thread_, nullptr, &GuiReceiverThread::thread_func, this);
    if (result != 0)
        throw std::runtime_error("GuiReceiverThread 생성 실패");
    started_ = true;
}

void GuiReceiverThread::stop()
{
    receiver_.stop();
}

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

void GuiReceiverThread::run()
{
    Logger::info("[GuiReceiverThread] 시작");
    GuiCommand command;
    while (receiver_.receive(command))
    {
        if (command.type == GuiCommandType::SelectTrack)
            target_selector_.set_selected_id(command.track_id);
    }
    Logger::info("[GuiReceiverThread] 종료");
}
