#pragma once

#include <pthread.h>
#include <vector>

#include "common/threadsafequeue.hpp"
#include "common/stage_metric.hpp"
#include "target/target_selector.hpp"

// 모든 Track 결과를 선택 대상의 관측 결과로 변환하는 worker.
class TargetSelectionThread
{
private:
    TargetSelector& selector_;
    ThreadSafeQueue<TrackingResult>& input_queue_;
    ThreadSafeQueue<TargetSelection>& output_queue_;
    pthread_t thread_;
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    TargetSelectionThread(
        TargetSelector& selector,
        ThreadSafeQueue<TrackingResult>& input_queue,
        ThreadSafeQueue<TargetSelection>& output_queue,
        bool measurement_enabled
    );

    void start();
    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
