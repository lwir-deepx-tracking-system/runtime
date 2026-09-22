#pragma once

#include <pthread.h>
#include <vector>

#include "common/detection.hpp"
#include "common/track.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "tracking/tracker.hpp"

// Detection 목록을 받아 Tracking을 수행하고 Track 목록을 전달한다.
class TrackingThread
{
private:
    Tracker& tracker_;
    ThreadSafeQueue<DetectionResult>& input_queue_;
    ThreadSafeQueue<TrackingResult>& output_queue_;
    pthread_t thread_;
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    TrackingThread(
        Tracker& tracker,
        ThreadSafeQueue<DetectionResult>& input_queue,
        ThreadSafeQueue<TrackingResult>& output_queue,
        bool measurement_enabled
    );

    void start();
    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
