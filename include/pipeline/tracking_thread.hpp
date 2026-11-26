#pragma once

#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <cstdint>

#include "common/detection.hpp"
#include "common/track.hpp"
#include "common/stage_metric.hpp"
#include "common/measurement_sink.hpp"
#include "common/threadsafequeue.hpp"
#include "tracking/tracker_manager.hpp"
#include "pipeline/runtime_control.hpp"
class RawRecorder;
class PipelinePerformance;

struct AutoModelObservation {
    DetectionResult detections;
    TrackingResultPtr tracks;
};

// Detection을 Tracking하고 같은 결과를 GUI와 제어 경로에 분기한다.
class TrackingThread
{
private:
    TrackerManager& tracker_manager_;
    RuntimeControl& runtime_control_;
    std::uint64_t tracker_generation_ = ~std::uint64_t{0};
    std::uint64_t tracker_epoch_ = 0;
    ThreadSafeQueue<DetectionResult>& input_queue_;
    ThreadSafeQueue<TrackingResultPtr>& control_output_queue_;
    ThreadSafeQueue<TrackingResultPtr>& gui_output_queue_;
    std::thread thread_;
    bool started_ = false;
    bool measurement_enabled_;
    IMeasurementSink* measurements_;
    RawRecorder* recorder_ = nullptr;
    PipelinePerformance* performance_;
    ThreadSafeQueue<AutoModelObservation>* auto_output_queue_ = nullptr;
    std::atomic<bool> failed_{false};
    std::string error_message_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    TrackingThread(
        TrackerManager& tracker_manager,
        RuntimeControl& runtime_control,
        ThreadSafeQueue<DetectionResult>& input_queue,
        ThreadSafeQueue<TrackingResultPtr>& control_output_queue,
        ThreadSafeQueue<TrackingResultPtr>& gui_output_queue,
        bool measurement_enabled, IMeasurementSink* measurements = nullptr,
        RawRecorder* recorder = nullptr,
        ThreadSafeQueue<AutoModelObservation>* auto_output_queue = nullptr, PipelinePerformance* performance = nullptr
    );

    void start();
    void join();
    bool failed() const;
    const std::string& error_message() const;
    const std::vector<StageMetric>& metrics() const;
};
