#pragma once

#include <pthread.h>

#include "common/threadsafequeue.hpp"
#include "common/stage_metric.hpp"
#include "communication/stm32_link.hpp"

// 선택 결과 queue를 소비하고 STM32Link에 전달하는 worker.
class CommunicationThread
{
private:
    STM32Link& link_;
    ThreadSafeQueue<TargetSelection>& input_queue_;
    pthread_t thread_;
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    CommunicationThread(STM32Link& link, ThreadSafeQueue<TargetSelection>& input_queue,
        bool measurement_enabled);
    void start();
    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
