#pragma once

#include <pthread.h>

#include "camera/camera.hpp"
#include "common/frame.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"


// Camera에서 Frame을 읽어 Frame Queue에 전달한다.
class CameraThread
{
private:
    Camera& camera_;
    ThreadSafeQueue<Frame>& output_queue_;
    pthread_t thread_;
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    CameraThread(
        Camera& camera,
        ThreadSafeQueue<Frame>& output_queue,
        bool measurement_enabled
    );

    void start();
    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
