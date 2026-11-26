#pragma once

#include <array>
#include <atomic>
#include <thread>
#include <string>

#include "app/camera_watchdog.hpp"
#include "camera/camera.hpp"
#include "config/app_config.hpp"
#include "common/frame.hpp"
#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
class RawRecorder;
class PipelinePerformance;


// Camera에서 FrameContext를 만들고 shared_ptr로 Detection에 전달한다.
class CameraThread
{
private:
    Camera& camera_;
    ThreadSafeQueue<FrameMessage>& output_queue_;
    std::thread thread_;
    bool started_ = false;
    bool measurement_enabled_;
    bool retain_metrics_;
    RawRecorder* recorder_ = nullptr;
    PipelinePerformance* performance_;
    CameraRecoveryConfig recovery_;
    CameraWatchdog watchdog_;
    std::array<std::uint16_t, 64> previous_scene_{};
    unsigned identical_ = 0;
    bool have_scene_ = false;
    std::atomic<bool> reopen_requested_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> failed_{false};
    std::string error_message_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();
    bool open_camera();
    bool reopen_camera(const char* reason, unsigned& attempts);
    bool repeated_scene(const cv::Mat& image);

public:
    CameraThread(
        Camera& camera,
        ThreadSafeQueue<FrameMessage>& output_queue,
        bool measurement_enabled,
        bool retain_metrics = true,
        RawRecorder* recorder = nullptr, PipelinePerformance* performance = nullptr,
        CameraRecoveryConfig recovery = {}
    );

    void start();
    void join();
    void request_stop();
    bool request_reopen();
    bool finished() const;
    bool failed() const;
    const std::string& error_message() const;
    const std::vector<StageMetric>& metrics() const;
};
