#include <memory>
#include <vector>

#include <opencv2/core.hpp>

#include "common/detection.hpp"
#include "common/frame.hpp"
#include "common/threadsafequeue.hpp"
#include "detection/dxapp_detection_pipeline.hpp"
#include "pipeline/detection_thread.hpp"
#include "pipeline/tracking_thread.hpp"
#include "tracking/tracker.hpp"

class FakeDetectionPipeline : public IDetectionPipeline
{
public:
    std::vector<Detection> detect(const FrameContext&) override
    {
        Detection detection;
        detection.width = 10.0f;
        detection.height = 10.0f;
        return {detection};
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

    FakeDetectionPipeline detector;
    FakeTracker tracker;
    DetectionThread detection_thread(
        detector, frame_queue, detection_queue, false);
    TrackingThread tracking_thread(
        tracker, detection_queue, control_queue, gui_queue, false);

    tracking_thread.start();
    detection_thread.start();

    auto mutable_frame = std::make_shared<FrameContext>();
    mutable_frame->metadata.frame_id = 42;
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

    return 0;
}
