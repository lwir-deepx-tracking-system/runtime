#pragma once

#include <pthread.h>
#include <vector>
#include "common/detection.hpp"
#include "common/frame.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "detection/dxapp_detection_pipeline.hpp"

class DetectionThread {
public:
    DetectionThread(DetectionPipeline& pipeline, ThreadSafeQueue<Frame>& input,
                    ThreadSafeQueue<DetectionResult>& output, bool measurement_enabled);
    void start();
    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
private:
    static void* thread_func(void* argument);
    void run();
    DetectionPipeline& pipeline_;
    ThreadSafeQueue<Frame>& input_queue_;
    ThreadSafeQueue<DetectionResult>& output_queue_;
    pthread_t thread_{};
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;
};
