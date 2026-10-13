#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

#include "common/detection.hpp"
#include "common/frame.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "detection/i_detection_pipeline.hpp"

class DetectionThread
{
private:
    IDetectionPipeline& pipeline_;
    ThreadSafeQueue<FrameMessage>& input_queue_;
    ThreadSafeQueue<DetectionResult>& output_queue_;
    pthread_t thread_{};
    bool started_ = false;
    bool measurement_enabled_;
    std::atomic<bool> failed_{false};
    mutable std::mutex state_mutex_;
    std::string error_message_;
    std::mutex metrics_mutex_;
    std::vector<StageMetric> metrics_;
    std::mutex reorder_mutex_;
    std::map<std::uint64_t, DetectionResult> completed_results_;
    std::uint64_t next_submit_sequence_ = 0;
    std::uint64_t next_output_sequence_ = 0;

    static void* thread_func(void* arg);
    void run();
    void set_error(const std::string& message);

public:
    DetectionThread(
        IDetectionPipeline& pipeline,
        ThreadSafeQueue<FrameMessage>& input_queue,
        ThreadSafeQueue<DetectionResult>& output_queue,
        bool measurement_enabled);

    void start();
    void join();
    bool failed() const { return failed_.load(); }
    const std::string& error_message() const { return error_message_; }
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
