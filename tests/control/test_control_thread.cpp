#include <chrono>
#include <memory>

#include "common/threadsafequeue.hpp"
#include "common/track.hpp"
#include "control/gimbal_controller.hpp"
#include "pipeline/control_thread.hpp"
#include "target/target_selector.hpp"

int main()
{
    ThreadSafeQueue<TrackingResultPtr> input_queue;
    TargetSelector target_selector;
    GimbalController controller;
    ControlThread thread(
        controller, target_selector, input_queue, true, true);

    target_selector.set_selected_id(7);
    thread.start();

    auto matched = std::make_shared<TrackingResult>();
    auto matched_frame = std::make_shared<FrameContext>();
    matched_frame->metadata.frame_id = 1;
    matched_frame->metadata.captured_at = std::chrono::steady_clock::now();
    matched->frame = matched_frame;
    matched->tracks.push_back(Track{7});
    matched->enqueued_at = std::chrono::steady_clock::now();
    input_queue.push(matched);

    // 선택 ID가 현재 frame에 없어도 nullptr target으로 제어 경계를 호출한다.
    auto missing = std::make_shared<TrackingResult>();
    auto missing_frame = std::make_shared<FrameContext>();
    missing_frame->metadata.frame_id = 2;
    missing_frame->metadata.captured_at = std::chrono::steady_clock::now();
    missing->frame = missing_frame;
    missing->enqueued_at = std::chrono::steady_clock::now();
    input_queue.push(missing);

    input_queue.close();
    thread.join();

    const auto& metrics = thread.metrics();
    if (metrics.size() != 2) return 1;
    if (metrics[0].frame_id != 1 || metrics[1].frame_id != 2) return 2;
    if (!metrics[0].queue_wait_ms || !metrics[1].queue_wait_ms) return 3;
    if (!metrics[0].e2e_ms || !metrics[1].e2e_ms) return 4;
    return 0;
}
