#include "pipeline/model_manager_thread.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

#include "common/logger.hpp"
#include "detection/detector_factory.hpp"

ModelManagerThread::ModelManagerThread(
    ModelCatalogEntry initial_model,
    DetectionConfig detection_config,
    ThreadSafeQueue<ModelChangeRequest>& requests,
    ActivePipelineSlot& active_pipeline,
    RuntimeControl& runtime_control,
    ThreadSafeQueue<ModelChangeResult>* results,
    std::function<std::shared_ptr<IDetectionPipeline>(const ModelCatalogEntry&)>
        pipeline_creator)
    : initial_model_(std::move(initial_model)),
      active_model_(initial_model_),
      detection_config_(std::move(detection_config)),
      requests_(requests),
      results_(results),
      active_pipeline_(active_pipeline),
      runtime_control_(runtime_control),
      pipeline_creator_(std::move(pipeline_creator))
{
}

void ModelManagerThread::start()
{
    if (started_) return;
    thread_ = std::thread(&ModelManagerThread::thread_func, this);
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
    if(thread_.joinable()) thread_.join();
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
    if (pipeline_creator_)
        return pipeline_creator_(entry);
    auto factory = DetectorFactory::for_model(entry.model);
    auto pipeline = factory->create(entry.model, detection_config_);
    return std::shared_ptr<IDetectionPipeline>(std::move(pipeline));
}

void ModelManagerThread::run()
{
    Logger::info("[ModelManagerThread] started");

    try
    {
        auto initial_pipeline = create_pipeline(initial_model_);
        if (!initial_pipeline)
            throw std::runtime_error("Initial model pipeline creation returned null");
        active_pipeline_.set(std::move(initial_pipeline));
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

    // The old DXNN instance must be released before loading another. If loading
    // fails, recreate the last working model from its validated configuration.
    const auto restore_previous_model = [this](
        const ModelChangeRequest& request, const std::string& failure) {
        Logger::error("[ModelManagerThread] model change failed: " + failure);
        if (runtime_control_.stop_requested.load()) return;
        try
        {
            auto restored = create_pipeline(active_model_);
            if (!restored)
                throw std::runtime_error("Rollback pipeline creation returned null");
            if (runtime_control_.stop_requested.load()) return;
            active_pipeline_.set(std::move(restored));
            active_model_id_.store(active_model_.id);
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                error_message_.clear();
            }
            runtime_control_.generation.fetch_add(1);
            runtime_control_.state.store(RuntimeState::Running);
            if (results_) results_->push({request.request_id, request.entry.id,
                active_model_.id, ModelChangeOutcome::Restored, failure});
            Logger::warn("[ModelManagerThread] restored model id=" +
                std::to_string(active_model_.id));
        }
        catch (const std::exception& e)
        {
            active_model_id_.store(0);
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                error_message_ = failure + "; rollback failed: " + e.what();
            }
            runtime_control_.state.store(RuntimeState::SwitchFailed);
            if (results_) results_->push({request.request_id, request.entry.id,
                0, ModelChangeOutcome::Failed, error_message()});
            Logger::error("[ModelManagerThread] " + error_message());
        }
        catch (...)
        {
            active_model_id_.store(0);
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                error_message_ = failure + "; rollback failed: unknown error";
            }
            runtime_control_.state.store(RuntimeState::SwitchFailed);
            if (results_) results_->push({request.request_id, request.entry.id,
                0, ModelChangeOutcome::Failed, error_message()});
            Logger::error("[ModelManagerThread] " + error_message());
        }
    };

    ModelChangeRequest request;
    while (requests_.pop(request))
    {
        if (runtime_control_.stop_requested.load())
            break;

        if (!runtime_control_.wait_detection_quiesced())
            break;

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
            if (!new_pipeline)
                throw std::runtime_error("Model pipeline creation returned null");
            if (runtime_control_.stop_requested.load()) break;
            active_model_ = request.entry;
            active_pipeline_.set(std::move(new_pipeline));
            active_model_id_.store(request.entry.id);
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                error_message_.clear();
            }
            runtime_control_.generation.fetch_add(1);
            runtime_control_.state.store(RuntimeState::Running);
            if (results_) results_->push({request.request_id, request.entry.id,
                request.entry.id, ModelChangeOutcome::Activated, {}});
            Logger::info(
                "[ModelManagerThread] model changed to id=" +
                std::to_string(request.entry.id) +
                " request=" + std::to_string(request.request_id));
        }
        catch (const std::exception& e)
        {
            old_pipeline.reset();
            restore_previous_model(request, e.what());
        }
        catch (...)
        {
            old_pipeline.reset();
            restore_previous_model(request, "Unknown model change error");
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
