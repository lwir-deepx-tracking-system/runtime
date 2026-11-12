#include "pipeline/tracker_change_service.hpp"
#include "tracking/tracker_manager.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>

namespace
{
// 추론 없이 생성 지연·실패와 이전 snapshot의 수명만 관찰한다.
class TestTracker final : public Tracker
{
  public:
    std::vector<Track> track(const std::vector<Detection> &) override { return {}; }
    void               reset() override {}
};

// 테스트 실패로 조기 반환해도 생성 작업의 대기를 풀어 합류를 보장한다.
struct Release
{
    std::promise<void> promise;
    bool               released = false;
    void               release()
    {
        if(!released)
        {
            released = true;
            promise.set_value();
        }
    }
    ~Release() { release(); }
};

// broken_promise 대신 명시적인 완료 결과가 제한 시간 안에 도착하는지 확인한다.
bool completed(const PipelineChangeTicket &ticket, PipelineResult expected)
{
    return ticket.completion.valid() &&
           ticket.completion.wait_for(std::chrono::seconds(2)) == std::future_status::ready &&
           ticket.completion.get() == expected;
}
} // namespace

// 비차단 접수, Busy, 생성 실패 보존, 생성 후 교체, 종료 중 완료와 재접수 차단을 검증한다.
int main()
{
    std::map<int, TrackingCatalogEntry> catalog;
    for(int id : {1, 2, 4})
    {
        TrackingCatalogEntry entry;
        entry.id                  = id;
        entry.config.track_buffer = id;
        catalog.emplace(id, entry);
    }
    Release                gate;
    auto                   released = gate.promise.get_future().share();
    std::promise<void>     entered;
    auto                   entered_future = entered.get_future();
    std::thread::id        creator_thread;
    std::atomic<bool>      block_once{true};
    std::atomic<int>       commits{0};
    TrackerManager         manager(catalog, 1, [&](const TrackingConfig &config) -> std::shared_ptr<Tracker>
                                   {
        if (config.track_buffer == 4) throw std::runtime_error("expected creation failure");
        if (config.track_buffer == 2 && block_once.exchange(false)) {
            creator_thread = std::this_thread::get_id();
            entered.set_value();
            released.wait();
        }
        return std::make_shared<TestTracker>(); });
    auto                   old      = manager.snapshot();
    std::weak_ptr<Tracker> previous = old.tracker;
    TrackerChangeService   service(manager, [&]
                                   { ++commits; });
    // service보다 먼저 파괴되어 대기를 해제해야 한다.
    struct Unblock
    {
        Release &gate;
        ~Unblock() { gate.release(); }
    } unblock{gate};
    if(service.request_change(2).result != PipelineResult::NotReady) return 1;
    service.start();
    if(service.request_change(3).result != PipelineResult::InvalidId ||
       service.request_change(0).result != PipelineResult::InvalidId) return 2;
    if(!completed(service.request_change(1), PipelineResult::Ok) || commits != 0) return 3;
    if(!completed(service.request_change(4), PipelineResult::CreationFailed) ||
       manager.active_id() != 1 || commits != 0) return 4;
    auto change = service.request_change(2);
    if(change.result != PipelineResult::Ok ||
       entered_future.wait_for(std::chrono::seconds(2)) != std::future_status::ready) return 5;
    if(creator_thread == std::this_thread::get_id() || manager.active_id() != 1 ||
       change.completion.wait_for(std::chrono::seconds(0)) == std::future_status::ready) return 6;
    if(service.request_change(1).result != PipelineResult::Busy) return 7;
    service.close();
    if(service.request_change(1).result != PipelineResult::NotReady) return 8;
    gate.release();
    if(!completed(change, PipelineResult::Ok)) return 9;
    service.join();
    service.close();
    service.join();
    if(manager.active_id() != 2 || commits != 1 || previous.expired()) return 10;
    old.tracker.reset();
    if(!previous.expired()) return 11;
    try
    {
        service.start();
        return 12;
    }
    catch(const std::logic_error &)
    {
    }
    TrackerChangeService policy_service(manager);
    policy_service.start();
    TrackerPolicyUpdate update;
    update.warmth_min = 8000;
    if (!completed(policy_service.request_policy_change(update), PipelineResult::Ok) ||
        manager.active_config().frame.warmth_min != 8000) return 13;
    update.warmth_min = 70000;
    if (!completed(policy_service.request_policy_change(update), PipelineResult::InvalidConfiguration) ||
        manager.active_config().frame.warmth_min != 8000) return 14;
    policy_service.close();
    policy_service.join();
    return 0;
}
