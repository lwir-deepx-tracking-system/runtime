#include "tracking/tracker_manager.hpp"

#include <stdexcept>
#include <cmath>
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
        active_config_ = entry->second.config;
        ++active_.epoch;
        if (published_epoch_) published_epoch_->store(active_.epoch);
    }
    // 프레임 처리 중인 snapshot이 있으면 이전 트래커는 그 뒤에 소멸한다.
    previous.reset();
}

void TrackerManager::update_active_policy(const TrackerPolicyUpdate& update,
    const std::function<void()>& before_commit)
{
    std::lock_guard<std::mutex> switch_lock(switch_mutex_);
    TrackingConfig config;
    {
        std::lock_guard<std::mutex> active_lock(active_mutex_);
        if (!active_.tracker) throw std::runtime_error("No active tracker");
        config = active_config_;
    }
    if (update.warmth_min) config.frame.warmth_min = *update.warmth_min;
    if (update.warmth_max) config.frame.warmth_max = *update.warmth_max;
    if (update.edge_birth) config.edge_birth_threshold = *update.edge_birth;
    if (update.confirmation_ms) config.frame.confirmation_ms = *update.confirmation_ms;
    if (update.gmc) config.frame.gmc = *update.gmc;
    if (update.class_gate) config.class_gate = *update.class_gate;
    if (config.frame.warmth_min < 0 || config.frame.warmth_min > 65535 ||
        config.frame.warmth_max < 0 || config.frame.warmth_max > 65535 ||
        (config.frame.warmth_max > 0 && config.frame.warmth_min > config.frame.warmth_max) ||
        !std::isfinite(config.edge_birth_threshold) || config.edge_birth_threshold < 0 ||
        config.edge_birth_threshold > 1 || config.frame.confirmation_ms < 0 ||
        config.frame.confirmation_ms > 1000)
        throw std::invalid_argument("Invalid tracker policy update");
    std::shared_ptr<Tracker> candidate = creator_(config);
    if (!candidate) throw std::runtime_error("Tracker factory returned null");
    if (before_commit) before_commit();
    std::shared_ptr<Tracker> previous;
    {
        std::lock_guard<std::mutex> active_lock(active_mutex_);
        previous = std::move(active_.tracker);
        active_.tracker = std::move(candidate);
        active_config_ = std::move(config);
        ++active_.epoch;
        if (published_epoch_) published_epoch_->store(active_.epoch);
    }
    previous.reset();
}

TrackerManager::Snapshot TrackerManager::snapshot() const
{
    std::lock_guard<std::mutex> lock(active_mutex_);
    return active_;
}

TrackingConfig TrackerManager::active_config() const
{
    std::lock_guard<std::mutex> lock(active_mutex_);
    return active_config_;
}

std::pair<TrackerManager::Snapshot, TrackingConfig> TrackerManager::policy_snapshot() const
{
    std::lock_guard<std::mutex> lock(active_mutex_);
    return {active_, active_config_};
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
