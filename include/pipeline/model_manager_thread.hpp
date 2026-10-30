#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>

#include "common/threadsafequeue.hpp"
#include "config/app_config.hpp"
#include "detection/i_detection_pipeline.hpp"
#include "pipeline/runtime_control.hpp"
#include "pipeline/active_pipeline_slot.hpp"

struct ModelCatalogEntry
{
    int id = 0;
    std::string config_path;
    ModelConfig model;
};

struct ModelChangeRequest
{
    std::uint64_t request_id = 0;
    ModelCatalogEntry entry;
};

class ModelManagerThread
{
private:
    ModelCatalogEntry initial_model_;
    DetectionConfig detection_config_;
    ThreadSafeQueue<ModelChangeRequest>& requests_;
    ActivePipelineSlot& active_pipeline_;
    RuntimeControl& runtime_control_;
    pthread_t thread_{};
    bool started_ = false;
    std::atomic<bool> failed_{false};
    mutable std::mutex status_mutex_;
    std::condition_variable initialized_condition_;
    bool initialized_ = false;
    std::string error_message_;
    std::atomic<int> active_model_id_{0};

    static void* thread_func(void* arg);
    void run();
    std::shared_ptr<IDetectionPipeline> create_pipeline(
        const ModelCatalogEntry& entry) const;
    void set_error(const std::string& message);

public:
    ModelManagerThread(
        ModelCatalogEntry initial_model,
        DetectionConfig detection_config,
        ThreadSafeQueue<ModelChangeRequest>& requests,
        ActivePipelineSlot& active_pipeline,
        RuntimeControl& runtime_control);

    void start();
    void wait_until_initialized();
    void join();

    bool failed() const { return failed_.load(); }
    int active_model_id() const { return active_model_id_.load(); }
    std::string error_message() const;
};
