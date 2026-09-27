#pragma once

#include <pthread.h>
#include <vector>

#include "common/detection.hpp"
#include "common/track.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "tracking/tracker.hpp"

// Detection을 Tracking하고 같은 결과를 GUI와 제어 경로에 분기한다.
class TrackingThread
{
private:
    Tracker& tracker_;
    ThreadSafeQueue<DetectionResult>& input_queue_;
    ThreadSafeQueue<TrackingResultPtr>& control_output_queue_;
    ThreadSafeQueue<TrackingResultPtr>& gui_output_queue_;
    pthread_t thread_;
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    TrackingThread(
        Tracker& tracker,
        ThreadSafeQueue<DetectionResult>& input_queue,
        ThreadSafeQueue<TrackingResultPtr>& control_output_queue,
        ThreadSafeQueue<TrackingResultPtr>& gui_output_queue,
        bool measurement_enabled
    );

    void start();
    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
