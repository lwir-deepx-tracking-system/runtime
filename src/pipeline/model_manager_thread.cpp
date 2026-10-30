#include "pipeline/model_manager_thread.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

#include "common/logger.hpp"
#include "detection/detector_factory.hpp"

std::shared_ptr<IDetectionPipeline> ActivePipelineSlot::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pipeline_;
}

std::shared_ptr<IDetectionPipeline> ActivePipelineSlot::take()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(pipeline_);
}

void ActivePipelineSlot::set(std::shared_ptr<IDetectionPipeline> pipeline)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_ = std::move(pipeline);
}

ModelManagerThread::ModelManagerThread(
    ModelCatalogEntry initial_model,
    DetectionConfig detection_config,
    ThreadSafeQueue<ModelChangeRequest>& requests,
    ActivePipelineSlot& active_pipeline,
    RuntimeControl& runtime_control)
    : initial_model_(std::move(initial_model)),
      detection_config_(std::move(detection_config)),
      requests_(requests),
      active_pipeline_(active_pipeline),
      runtime_control_(runtime_control)
{
}

void ModelManagerThread::start()
{
    if (started_) return;
    const int result = pthread_create(
        &thread_, nullptr, &ModelManagerThread::thread_func, this);
    if (result != 0)
        throw std::runtime_error("ModelManagerThread start failed");
    started_ = true;
}

void ModelManagerThread::wait_until_initialized()
{
    std::unique_lock<std::mutex> lock(status_mutex_);
    initialized_condition_.wait(lock, [this] { return initialized_; });
    if (failed_.load())
        throw std::runtime_error(error_message_);
}

void ModelManagerThread::join()
{
    if (!started_) return;
    pthread_join(thread_, nullptr);
    started_ = false;
}

std::string ModelManagerThread::error_message() const
{
    std::lock_guard<std::mutex> lock(status_mutex_);
    return error_message_;
}

void* ModelManagerThread::thread_func(void* arg)
{
    static_cast<ModelManagerThread*>(arg)->run();
    return nullptr;
}

void ModelManagerThread::set_error(const std::string& message)
{
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (error_message_.empty())
        error_message_ = message;
    failed_.store(true);
}

std::shared_ptr<IDetectionPipeline> ModelManagerThread::create_pipeline(
    const ModelCatalogEntry& entry) const
{
    auto factory = DetectorFactory::for_model(entry.model);
    auto pipeline = factory->create(entry.model, detection_config_);
    return std::shared_ptr<IDetectionPipeline>(std::move(pipeline));
}

void ModelManagerThread::run()
{
    Logger::info("[ModelManagerThread] started");

    try
    {
        active_pipeline_.set(create_pipeline(initial_model_));
        active_model_id_.store(initial_model_.id);
        runtime_control_.generation.store(0);
        runtime_control_.state.store(RuntimeState::Running);
    }
    catch (const std::exception& e)
    {
        set_error(e.what());
        runtime_control_.state.store(RuntimeState::SwitchFailed);
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            initialized_ = true;
        }
        initialized_condition_.notify_all();
        Logger::error("[ModelManagerThread] initial model load failed: " +
            error_message());
        return;
    }
    catch (...)
    {
        set_error("Unknown initial model creation error");
        runtime_control_.state.store(RuntimeState::SwitchFailed);
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            initialized_ = true;
        }
        initialized_condition_.notify_all();
        Logger::error("[ModelManagerThread] initial model load failed");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        initialized_ = true;
    }
    initialized_condition_.notify_all();
    Logger::info(
        "[ModelManagerThread] active model id=" +
        std::to_string(active_model_id_.load()));

    ModelChangeRequest request;
    while (requests_.pop(request))
    {
        if (runtime_control_.stop_requested.load())
            break;

        runtime_control_.wait_detection_quiesced();

        std::shared_ptr<IDetectionPipeline> old_pipeline =
            active_pipeline_.take();
        try
        {
            if (old_pipeline)
                old_pipeline->wait_idle();
            old_pipeline.reset();

            if (runtime_control_.stop_requested.load())
                break;

            std::shared_ptr<IDetectionPipeline> new_pipeline =
                create_pipeline(request.entry);
            active_pipeline_.set(std::move(new_pipeline));
            active_model_id_.store(request.entry.id);
            runtime_control_.generation.fetch_add(1);
            runtime_control_.state.store(RuntimeState::Running);
            Logger::info(
                "[ModelManagerThread] model changed to id=" +
                std::to_string(request.entry.id) +
                " request=" + std::to_string(request.request_id));
        }
        catch (const std::exception& e)
        {
            old_pipeline.reset();
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                error_message_ = e.what();
            }
            runtime_control_.state.store(RuntimeState::SwitchFailed);
            Logger::error(
                "[ModelManagerThread] model change failed: " +
                std::string(e.what()));
        }
        catch (...)
        {
            old_pipeline.reset();
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                error_message_ = "Unknown model change error";
            }
            runtime_control_.state.store(RuntimeState::SwitchFailed);
            Logger::error("[ModelManagerThread] model change failed");
        }
    }

    std::shared_ptr<IDetectionPipeline> remaining =
        active_pipeline_.take();
    if (remaining)
    {
        try
        {
            remaining->wait_idle();
        }
        catch (const std::exception& e)
        {
            Logger::error(
                "[ModelManagerThread] final inference drain failed: " +
                std::string(e.what()));
        }
        catch (...)
        {
            Logger::error("[ModelManagerThread] final inference drain failed");
        }
    }
    remaining.reset();
    Logger::info("[ModelManagerThread] stopped");
}
