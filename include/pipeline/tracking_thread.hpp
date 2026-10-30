#pragma once

#include <atomic>
#include <pthread.h>
#include <string>
#include <vector>
#include <cstdint>

#include "common/detection.hpp"
#include "common/track.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "tracking/tracker.hpp"
#include "pipeline/runtime_control.hpp"

// Detection을 Tracking하고 같은 결과를 GUI와 제어 경로에 분기한다.
class TrackingThread
{
private:
    Tracker& tracker_;
    RuntimeControl& runtime_control_;
    std::uint64_t tracker_generation_ = ~std::uint64_t{0};
    ThreadSafeQueue<DetectionResult>& input_queue_;
    ThreadSafeQueue<TrackingResultPtr>& control_output_queue_;
    ThreadSafeQueue<TrackingResultPtr>& gui_output_queue_;
    pthread_t thread_{};
    bool started_ = false;
    bool measurement_enabled_;
    std::atomic<bool> failed_{false};
    std::string error_message_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    TrackingThread(
        Tracker& tracker,
        RuntimeControl& runtime_control,
        ThreadSafeQueue<DetectionResult>& input_queue,
        ThreadSafeQueue<TrackingResultPtr>& control_output_queue,
        ThreadSafeQueue<TrackingResultPtr>& gui_output_queue,
        bool measurement_enabled
    );

    void start();
    void join();
    bool failed() const { return failed_.load(); }
    const std::string& error_message() const { return error_message_; }
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
