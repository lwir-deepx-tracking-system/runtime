#pragma once

#include <pthread.h>

#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "control/gimbal_controller.hpp"

// 선택 결과 queue를 소비하고 Orange Pi의 짐벌 제어기에 전달하는 worker.
class ControlThread
{
private:
    GimbalController& controller_;
    ThreadSafeQueue<TargetSelection>& input_queue_;
    pthread_t thread_;
    bool control_enabled_;
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    ControlThread(GimbalController& controller,
        ThreadSafeQueue<TargetSelection>& input_queue,
        bool control_enabled,
        bool measurement_enabled);
    void start();
    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
