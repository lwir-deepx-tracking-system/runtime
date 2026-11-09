#include "tracking/tracker_manager.hpp"

#include <stdexcept>
#include <utility>

#include "tracking/tracker_factory.hpp"

TrackerManager::TrackerManager(std::map<int, TrackingCatalogEntry> catalog,
    int initial_id, Creator creator, std::atomic<std::uint64_t>* published_epoch)
    : catalog_(std::move(catalog)), creator_(std::move(creator)),
      published_epoch_(published_epoch)
{
    if (!creator_)
        creator_ = [](const TrackingConfig& config) -> std::shared_ptr<Tracker> {
            auto factory = TrackerFactory::for_type(config.type);
            return factory->create(config);
        };
    activate(initial_id);
}

void TrackerManager::activate(int id, const std::function<void()>& before_commit)
{
    std::lock_guard<std::mutex> switch_lock(switch_mutex_);
    const auto entry = catalog_.find(id);
    if (entry == catalog_.end())
        throw std::invalid_argument("Unknown tracker id=" + std::to_string(id));
    if (active_id() == id) return;

    // 생성은 활성 참조 잠금 밖에서 수행해 기존 트래커가 계속 프레임을 처리하게 한다.
    std::shared_ptr<Tracker> candidate = creator_(entry->second.config);
    if (!candidate)
        throw std::runtime_error("Tracker factory returned null");
    if (before_commit) before_commit();

    std::shared_ptr<Tracker> previous;
    {
        std::lock_guard<std::mutex> active_lock(active_mutex_);
        previous = std::move(active_.tracker);
        active_.tracker = std::move(candidate);
        active_.id = id;
        ++active_.epoch;
        if (published_epoch_) published_epoch_->store(active_.epoch);
    }
    // 프레임 처리 중인 snapshot이 있으면 이전 트래커는 그 뒤에 소멸한다.
    previous.reset();
}

TrackerManager::Snapshot TrackerManager::snapshot() const
{
    std::lock_guard<std::mutex> lock(active_mutex_);
    return active_;
}

int TrackerManager::active_id() const
{
    std::lock_guard<std::mutex> lock(active_mutex_);
    return active_.id;
}

std::uint64_t TrackerManager::active_epoch() const
{
    std::lock_guard<std::mutex> lock(active_mutex_);
    return active_.epoch;
}
