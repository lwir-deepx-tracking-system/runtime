#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "pipeline/tracking_thread.hpp"
#include "tracking/tracker_manager.hpp"

namespace
{
struct ProbeState
{
    std::mutex mutex;
    std::condition_variable condition;
    bool old_entered = false;
    bool release_old = false;
    std::vector<int> events;
};

// 이전 트래커의 프레임 처리를 멈춰 교체 중 수명과 결과 게시 순서를 검증한다.
class ProbeTracker final : public Tracker
{
    int id_;
    std::shared_ptr<ProbeState> state_;

public:
    ProbeTracker(int id, std::shared_ptr<ProbeState> state)
        : id_(id), state_(std::move(state))
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->events.push_back(id_);
    }

    ~ProbeTracker() override
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->events.push_back(-id_);
    }

    std::vector<Track> track(const std::vector<Detection>&) override
    {
        if (id_ == 1)
        {
            std::unique_lock<std::mutex> lock(state_->mutex);
            state_->old_entered = true;
            state_->condition.notify_all();
            state_->condition.wait(lock, [&] { return state_->release_old; });
        }
        Track result;
        result.track_id = id_;
        return {result};
    }

    void reset() override {}
};

DetectionResult frame(std::uint64_t id)
{
    auto context = std::make_shared<FrameContext>();
    context->metadata.frame_id = id;
    DetectionResult result;
    result.frame = context;
    return result;
}
}  // namespace

// 새 트래커 생성, 원자적 교체, 이전 프레임 폐기, 실패 시 기존 인스턴스 유지를 검증한다.
int main()
{
    auto state = std::make_shared<ProbeState>();
    std::map<int, TrackingCatalogEntry> catalog;
    for (int id : {1, 2, 3})
    {
        TrackingCatalogEntry entry;
        entry.id = id;
        entry.config.track_buffer = id;
        catalog.emplace(id, entry);
    }
    RuntimeControl control;
    TrackerManager manager(std::move(catalog), 1,
        [state](const TrackingConfig& config) -> std::shared_ptr<Tracker> {
            if (config.track_buffer == 3)
                throw std::runtime_error("construction failed");
            return std::make_shared<ProbeTracker>(config.track_buffer, state);
        }, &control.tracker_epoch);

    ThreadSafeQueue<DetectionResult> input;
    ThreadSafeQueue<TrackingResultPtr> control_output;
    ThreadSafeQueue<TrackingResultPtr> gui_output;
    TrackingThread worker(manager, control, input, control_output, gui_output, false);
    struct WorkerCleanup
    {
        ProbeState& state;
        ThreadSafeQueue<DetectionResult>& input;
        TrackingThread& worker;
        ~WorkerCleanup()
        {
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                state.release_old = true;
            }
            state.condition.notify_all();
            input.close();
            worker.join();
        }
    } cleanup{*state, input, worker};
    worker.start();
    input.push(frame(1));

    {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->condition.wait_for(lock, std::chrono::seconds(2),
                [&] { return state->old_entered; }))
            return 1;
    }

    const auto old_epoch = manager.active_epoch();
    manager.activate(2);
    if (manager.active_id() != 2 || manager.active_epoch() == old_epoch ||
        control.tracker_epoch.load() != manager.active_epoch())
        return 2;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->events != std::vector<int>({1, 2})) return 3;
    }
    try { manager.activate(3); return 4; }
    catch (const std::runtime_error&) {}
    try { manager.activate(99); return 5; }
    catch (const std::invalid_argument&) {}
    if (manager.active_id() != 2) return 6;

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->release_old = true;
    }
    state->condition.notify_all();
    input.push(frame(2));
    input.close();
    worker.join();
    if (worker.failed()) return 7;

    TrackingResultPtr result;
    if (!control_output.pop(result) || result->frame->metadata.frame_id != 2 ||
        result->tracks.size() != 1 || result->tracks[0].track_id != 2 ||
        result->tracker_epoch != manager.active_epoch())
        return 8;
    if (control_output.pop(result)) return 9;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->events != std::vector<int>({1, 2, -1})) return 10;
    }
    return 0;
}
