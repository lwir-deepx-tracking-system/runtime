#include "pipeline/runtime_control.hpp"

void RuntimeControl::set_detection_quiesced(bool value)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        detection_quiesced_ = value;
    }
    condition_.notify_all();
}

void RuntimeControl::wait_detection_quiesced()
{
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] { return detection_quiesced_; });
}

void RuntimeControl::request_stop()
{
    stop_requested.store(true);
    state.store(RuntimeState::Stopping);
    condition_.notify_all();
}
