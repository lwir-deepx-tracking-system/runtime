#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <string>

#include "common/threadsafequeue.hpp"
#include "config/app_config.hpp"
#include "detection/i_detection_pipeline.hpp"
#include "pipeline/runtime_control.hpp"
#include "pipeline/active_pipeline_slot.hpp"

// 모델 변경 요청의 식별자와 교체할 모델 설정을 전달한다.
struct ModelChangeRequest
{
    std::uint64_t request_id = 0;
    ModelCatalogEntry entry;
};

enum class ModelChangeOutcome { Activated, Restored, Failed };
// 변경 결과와 복구 여부를 요청자에게 전달한다.
struct ModelChangeResult
{
    std::uint64_t request_id = 0;
    int requested_model_id = 0;
    int active_model_id = 0;
    ModelChangeOutcome outcome = ModelChangeOutcome::Failed;
    std::string error;
};

class ModelManagerThread
{
private:
    ModelCatalogEntry initial_model_;
    ModelCatalogEntry active_model_;
    DetectionConfig detection_config_;
    ThreadSafeQueue<ModelChangeRequest>& requests_;
    ThreadSafeQueue<ModelChangeResult>* results_;
    ActivePipelineSlot& active_pipeline_;
    RuntimeControl& runtime_control_;
    std::thread thread_;
    bool started_ = false;
    std::atomic<bool> failed_{false};
    mutable std::mutex status_mutex_;
    std::condition_variable initialized_condition_;
    bool initialized_ = false;
    std::string error_message_;
    std::atomic<int> active_model_id_{0};
    std::function<std::shared_ptr<IDetectionPipeline>(const ModelCatalogEntry&)>
        pipeline_creator_;

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
        RuntimeControl& runtime_control,
        ThreadSafeQueue<ModelChangeResult>* results = nullptr,
        std::function<std::shared_ptr<IDetectionPipeline>(const ModelCatalogEntry&)>
            pipeline_creator = {});

    void start();
    void wait_until_initialized();
    void join();

    bool failed() const { return failed_.load(); }
    int active_model_id() const { return active_model_id_.load(); }
    std::string error_message() const;
};
