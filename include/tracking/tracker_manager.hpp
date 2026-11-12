#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

#include "config/app_config.hpp"
#include "tracking/tracker.hpp"
#include "tracking/tracker_policy_update.hpp"

// 새 트래커를 생성한 뒤 활성 참조를 교체하고, 사용 중인 이전 인스턴스의 수명을 보존한다.
class TrackerManager
{
public:
    struct Snapshot
    {
        std::shared_ptr<Tracker> tracker;
        int id = 0;
        std::uint64_t epoch = 0;
    };
    using Creator = std::function<std::shared_ptr<Tracker>(const TrackingConfig&)>;

    TrackerManager(std::map<int, TrackingCatalogEntry> catalog,
        int initial_id, Creator creator = {},
        std::atomic<std::uint64_t>* published_epoch = nullptr);

    // 새 인스턴스 생성 후 before_commit을 실행하고 교체한다; 실패하면 기존 인스턴스를 유지한다.
    void activate(int id, const std::function<void()>& before_commit = {});
    void update_active_policy(const TrackerPolicyUpdate& update,
        const std::function<void()>& before_commit = {});
    Snapshot snapshot() const;
    TrackingConfig active_config() const;
    std::pair<Snapshot, TrackingConfig> policy_snapshot() const;
    int active_id() const;
    // 생성 시 확정된 불변 카탈로그에서 요청 ID의 존재 여부를 확인한다.
    bool contains(int id) const { return catalog_.count(id) != 0; }
    std::uint64_t active_epoch() const;

    // 결과 게시와 인스턴스 교체의 순서를 고정해 교체 뒤 이전 결과가 나오지 않게 한다.
    template <class Publish>
    bool publish_if_current(std::uint64_t epoch, Publish&& publish) const
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        if (active_.epoch != epoch) return false;
        std::forward<Publish>(publish)();
        return true;
    }

private:
    std::map<int, TrackingCatalogEntry> catalog_;
    Creator creator_;
    std::atomic<std::uint64_t>* published_epoch_;
    mutable std::mutex switch_mutex_;
    mutable std::mutex active_mutex_;
    Snapshot active_;
    TrackingConfig active_config_;
};
