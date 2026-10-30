#pragma once

#include <atomic>
#include <pthread.h>
#include <string>

#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "common/track.hpp"
#include "control/gimbal_controller.hpp"
#include "target/target_selector.hpp"

// Tracking 결과와 현재 선택 ID를 결합해 Orange Pi 짐벌 제어기에 전달한다.
class ControlThread
{
private:
    GimbalController& controller_;
    TargetSelector& target_selector_;
    ThreadSafeQueue<TrackingResultPtr>& input_queue_;
    pthread_t thread_{};
    bool started_ = false;
    bool control_enabled_;
    bool measurement_enabled_;
    std::atomic<bool> failed_{false};
    std::string error_message_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    ControlThread(
        GimbalController& controller,
        TargetSelector& target_selector,
        ThreadSafeQueue<TrackingResultPtr>& input_queue,
        bool control_enabled,
        bool measurement_enabled);
    void start();
    void join();
    bool failed() const { return failed_.load(); }
    const std::string& error_message() const { return error_message_; }
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
