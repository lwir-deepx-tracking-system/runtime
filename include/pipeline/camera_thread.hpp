#pragma once

#include <atomic>
#include <pthread.h>
#include <string>

#include "camera/camera.hpp"
#include "common/frame.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"


// Camera에서 FrameContext를 만들고 shared_ptr로 Detection에 전달한다.
class CameraThread
{
private:
    Camera& camera_;
    ThreadSafeQueue<FrameMessage>& output_queue_;
    pthread_t thread_{};
    bool started_ = false;
    bool measurement_enabled_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> failed_{false};
    std::string error_message_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    CameraThread(
        Camera& camera,
        ThreadSafeQueue<FrameMessage>& output_queue,
        bool measurement_enabled
    );

    void start();
    void join();
    void request_stop();
    bool finished() const { return finished_.load(); }
    bool failed() const { return failed_.load(); }
    const std::string& error_message() const { return error_message_; }
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
