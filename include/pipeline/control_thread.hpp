#pragma once

#include <atomic>
#include <thread>
#include <string>

#include "common/stage_metric.hpp"
#include "common/threadsafequeue.hpp"
#include "common/track.hpp"
#include "control/gimbal_command_executor.hpp"


// Tracking 결과를 단일 제어 실행 경계의 최신 결과 mailbox로 전달한다.
class ControlThread
{
private:
    GimbalCommandExecutor* executor_;
    ThreadSafeQueue<TrackingResultPtr>& input_queue_;
    std::thread thread_;
    bool started_ = false;
    bool measurement_enabled_;
    std::atomic<bool> failed_{false};
    std::string error_message_;
    std::vector<StageMetric> metrics_;

    static void* thread_func(void* arg);
    void run();

public:
    ControlThread(
        ThreadSafeQueue<TrackingResultPtr>& input_queue,
        GimbalCommandExecutor* executor,
        bool measurement_enabled);
    void start();
    void join();
    bool failed() const { return failed_.load(); }
    const std::string& error_message() const { return error_message_; }
    const std::vector<StageMetric>& metrics() const { return metrics_; }
};
