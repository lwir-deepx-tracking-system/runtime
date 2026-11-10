#include "pipeline/tracker_change_service.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

#include "common/logger.hpp"
#include "tracking/tracker_manager.hpp"

TrackerChangeService::TrackerChangeService(TrackerManager &manager, std::function<void()> before_commit)
    : manager_(manager), before_commit_(std::move(before_commit)) {}

// 예외 경로에서도 manager와 콜백 대상이 파괴되기 전에 작업자를 합류시킨다.
TrackerChangeService::~TrackerChangeService()
{
    close();
    join();
}

void TrackerChangeService::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(started_ || closed_) throw std::logic_error("Tracker change service cannot restart");
    worker_  = std::thread([this]
                           { run(); });
    started_ = true;
}

PipelineChangeTicket TrackerChangeService::request_change(std::uint32_t id)
{
    if(id == 0 || id > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
       !manager_.contains(static_cast<int>(id))) return {PipelineResult::InvalidId, {}};
    std::lock_guard<std::mutex> lock(mutex_);
    if(!started_ || closed_) return {PipelineResult::NotReady, {}};
    if(busy_) return {PipelineResult::Busy, {}};
    Request request{static_cast<int>(id), {}};
    auto    future = request.completion.get_future().share();
    pending_.emplace(std::move(request));
    busy_ = true;
    wake_.notify_one();
    return {PipelineResult::Ok, std::move(future)};
}

void TrackerChangeService::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    if(pending_)
    {
        pending_->completion.set_value(PipelineResult::NotReady);
        pending_.reset();
        busy_ = false;
    }
    wake_.notify_all();
}

void TrackerChangeService::join()
{
    if(worker_.joinable()) worker_.join();
}

void TrackerChangeService::run()
{
    for(;;)
    {
        std::optional<Request> request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this]
                       { return closed_ || pending_.has_value(); });
            if(closed_) return;
            request.emplace(std::move(*pending_));
            pending_.reset();
        }
        auto result = PipelineResult::Ok;
        try
        {
            manager_.activate(request->id, before_commit_);
            Logger::info("[TrackerChangeService] active tracker id=" + std::to_string(request->id));
        }
        catch(const std::exception &error)
        {
            result = PipelineResult::CreationFailed;
            Logger::error("[TrackerChangeService] tracker change failed id=" +
                          std::to_string(request->id) + ": " + error.what());
        }
        catch(...)
        {
            result = PipelineResult::CreationFailed;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        busy_ = false;
        request->completion.set_value(result);
    }
}
