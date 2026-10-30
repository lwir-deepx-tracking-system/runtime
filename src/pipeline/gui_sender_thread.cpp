#include "pipeline/gui_sender_thread.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>

#include "common/logger.hpp"

GuiSenderThread::GuiSenderThread(
    GuiSender& sender,
    RuntimeControl& runtime_control,
    ThreadSafeQueue<TrackingResultPtr>& input_queue)
    : sender_(sender),
      runtime_control_(runtime_control),
      input_queue_(input_queue)
{
}

void GuiSenderThread::start()
{
    if (started_) return;
    const int result = pthread_create(
        &thread_, nullptr, &GuiSenderThread::thread_func, this);
    if (result != 0)
        throw std::runtime_error("GuiSenderThread start failed");
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
    Logger::info("[GuiSenderThread] started");
    TrackingResultPtr result;

    try
    {
        while (input_queue_.pop(result))
        {
            if (!result || !result->frame) continue;
            const RuntimeState state = runtime_control_.state.load();
            bool sent = true;

            const auto gui_started_at = std::chrono::system_clock::now();
            const std::uint64_t gui_started_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    gui_started_at.time_since_epoch()).count());

            if (result->bypass)
            {
                if (state == RuntimeState::Switching ||
                    state == RuntimeState::SwitchFailed)
                    sent = sender_.send_bypass(result->frame, gui_started_us);
            }
            else if (state == RuntimeState::Running &&
                     result->model_generation == runtime_control_.generation.load())
                sent = sender_.send(*result, gui_started_us);

            if (!sent)
            {
                error_message_ = "GUI send failed";
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
        error_message_ = "Unknown fatal error";
        failed_.store(true);
        input_queue_.close();
        Logger::error("[GuiSenderThread] " + error_message_);
    }
    sender_.stop();
    Logger::info("[GuiSenderThread] stopped");
}
