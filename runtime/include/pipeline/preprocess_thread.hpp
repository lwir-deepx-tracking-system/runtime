#pragma once

#include <pthread.h>

#include "common/frame.hpp"
#include "common/model_input.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "preprocess/preprocessor.hpp"

class PreprocessThread
{
private:
    Preprocessor& preprocessor_;

    ThreadSafeQueue<Frame>& input_queue_;
    ThreadSafeQueue<ModelInput>& output_queue_;

    pthread_t thread_;
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;

    // pthread가 처음 진입하는 함수
    static void* thread_func(void* arg);

    // Frame을 받아 전처리하고 다음 Queue로 전달
    void run();

public:
    // Preprocessor와 입출력 Queue를 Thread에 연결
    PreprocessThread(
        Preprocessor& preprocessor,
        ThreadSafeQueue<Frame>& input_queue,
        ThreadSafeQueue<ModelInput>& output_queue,
        bool measurement_enabled);

    // pthread 실행
    void start();

    // pthread 종료 대기
    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
