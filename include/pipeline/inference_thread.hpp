#pragma once

#include <pthread.h>

#include "common/model_input.hpp"
#include "common/model_output.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "inference/inference.hpp"


// ModelInput을 받아 Inference를 실행하고 ModelOutput을 다음 Queue로 전달한다.
class InferenceThread
{
private:
    Inference& inference_;

    ThreadSafeQueue<ModelInput>& input_queue_;
    ThreadSafeQueue<ModelOutput>& output_queue_;

    pthread_t thread_;
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    InferenceThread(
        Inference& inference,
        ThreadSafeQueue<ModelInput>& input_queue,
        ThreadSafeQueue<ModelOutput>& output_queue,
        bool measurement_enabled
    );

    void start();
    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
