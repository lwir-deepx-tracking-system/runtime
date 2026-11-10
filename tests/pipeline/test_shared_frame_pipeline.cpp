#include <memory>
#include <vector>

#include <opencv2/core.hpp>

#include "common/detection.hpp"
#include "common/frame.hpp"
#include "common/threadsafequeue.hpp"
#include "detection/i_detection_pipeline.hpp"
#include "pipeline/detection_thread.hpp"
#include "pipeline/tracking_thread.hpp"
#include "tracking/tracker.hpp"
#include "tracking/tracker_manager.hpp"

// 저장 sink 사용 시 worker가 측정 벡터를 누적하지 않고 같은 세대를 전달하는지 확인한다.
class Measurements final : public IMeasurementSink {
    std::mutex mutex_;
public:
    std::vector<MetricStage> stages;
    unsigned detection_count = 0;
    bool invalid = false;
    void activate(std::uint64_t, int) noexcept override {}
    void record(std::uint64_t generation, MetricStage stage, const StageMetric& metric) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        invalid = invalid || generation != 0 || metric.frame_id != 42;
        stages.push_back(stage);
    }
    void record_detection(std::uint64_t generation, const DetectionTiming& timing) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        invalid = invalid || generation != 0 || timing.frame_id != 42;
        ++detection_count;
    }
};

class FakeDetectionPipeline : public IDetectionPipeline
{
public:
    // 비동기 미완료 작업이 없는 테스트 구현의 명시적 합류 계약.
    void wait_idle() noexcept override {}

    void submit_async(
        const FrameContextPtr& frame,
        Completion completion) override
    {
        Detection detection;
        detection.width = 10.0f;
        detection.height = 10.0f;

        DetectionTiming timing;
        timing.frame_id = frame->metadata.frame_id;
        completion({detection}, nullptr, timing);
    }
};

class FakeTracker : public Tracker
{
public:
    std::vector<Track> track(const std::vector<Detection>& detections) override
    {
        if (detections.empty())
            return {};

        Track track;
        track.track_id = 1;
        return {track};
    }

    void reset() override {}
};

int main()
{
    // GUI queue는 오래된 결과를 버리고 최신 두 개만 유지한다.
    ThreadSafeQueue<int> latest_queue;
    latest_queue.push_latest(1, 2);
    latest_queue.push_latest(2, 2);
    latest_queue.push_latest(3, 2);
    latest_queue.close();
    int first = 0;
    int second = 0;
    if (!latest_queue.pop(first) || !latest_queue.pop(second) ||
        first != 2 || second != 3)
        return 1;

    ThreadSafeQueue<FrameMessage> frame_queue;
    ThreadSafeQueue<DetectionResult> detection_queue;
    ThreadSafeQueue<TrackingResultPtr> control_queue;
    ThreadSafeQueue<TrackingResultPtr> gui_queue;

    RuntimeControl runtime_control;
    runtime_control.state.store(RuntimeState::Running);
    ActivePipelineSlot active_pipeline;
    active_pipeline.set(std::make_shared<FakeDetectionPipeline>());

    TrackingCatalogEntry tracker_entry;
    tracker_entry.id = 1;
    TrackerManager tracker_manager({{1, tracker_entry}}, 1,
        [](const TrackingConfig&) -> std::shared_ptr<Tracker> {
            return std::make_shared<FakeTracker>();
        });
    Measurements measurements;
    DetectionThread detection_thread(
        runtime_control,
        active_pipeline,
        frame_queue,
        detection_queue,
        gui_queue,
        false,
        true, &measurements);
    TrackingThread tracking_thread(
        tracker_manager,
        runtime_control,
        detection_queue,
        control_queue,
        gui_queue,
        true, &measurements);

    tracking_thread.start();
    detection_thread.start();

    auto mutable_frame = std::make_shared<FrameContext>();
    mutable_frame->metadata.frame_id = 42;
    mutable_frame->metadata.capture_processing_ms = 1.0;
    mutable_frame->image = cv::Mat::ones(2, 2, CV_16UC1);
    FrameContextPtr original_frame = mutable_frame;

    FrameMessage message;
    message.frame = original_frame;
    frame_queue.push(std::move(message));
    frame_queue.close();

    detection_thread.join();
    tracking_thread.join();

    TrackingResultPtr control_result;
    TrackingResultPtr gui_result;
    if (!control_queue.pop(control_result) || !gui_queue.pop(gui_result))
        return 1;

    if (!control_result || control_result != gui_result)
        return 1;
    if (control_result->frame != original_frame)
        return 1;
    if (control_result->frame->metadata.frame_id != 42)
        return 1;
    if (control_result->tracks.size() != 1)
        return 1;
    if (measurements.invalid || measurements.stages.size() != 3 || measurements.detection_count != 1 ||
        !detection_thread.metrics().empty() || !detection_thread.detection_timings().empty() ||
        !tracking_thread.metrics().empty()) return 2;

    return 0;
}
