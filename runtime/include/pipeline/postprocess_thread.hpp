#pragma once

#include <pthread.h>
#include <vector>

#include "common/detection.hpp"
#include "common/model_output.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "postprocess/postprocessor.hpp"


// ModelOutput을 후처리하고 Detection 목록을 다음 Stage로 전달한다.
class PostprocessThread
{
private:
    Postprocessor& postprocessor_;

    ThreadSafeQueue<ModelOutput>& input_queue_;

    ThreadSafeQueue<DetectionResult>& output_queue_;

    pthread_t thread_;
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;

    // pthread 진입 함수
    static void* thread_func(void* arg);

    // 실제 Postprocess Thread 실행 루프
    void run();

public:
    PostprocessThread(
        Postprocessor& postprocessor,
        ThreadSafeQueue<ModelOutput>& input_queue,
        ThreadSafeQueue<DetectionResult>& output_queue,
        bool measurement_enabled
    );

    void start();

    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
