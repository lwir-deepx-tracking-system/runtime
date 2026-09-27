#pragma once

#include <pthread.h>
#include <vector>

#include "common/detection.hpp"
#include "common/frame.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "detection/dxapp_detection_pipeline.hpp"

// Frame을 받아 DX 전처리, 추론, 후처리를 한 단계에서 수행한다.
class DetectionThread
{
private:
    DetectionPipeline& pipeline_;
    ThreadSafeQueue<FrameMessage>& input_queue_;
    ThreadSafeQueue<DetectionResult>& output_queue_;
    pthread_t thread_;
    bool measurement_enabled_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    DetectionThread(
        DetectionPipeline& pipeline,
        ThreadSafeQueue<FrameMessage>& input_queue,
        ThreadSafeQueue<DetectionResult>& output_queue,
        bool measurement_enabled);

    void start();
    void join();
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
