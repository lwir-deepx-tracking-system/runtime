#include <chrono>
#include <memory>

#include "common/threadsafequeue.hpp"
#include "common/track.hpp"
#include "control/gimbal_command_executor.hpp"
#include "pipeline/control_thread.hpp"
class NoMotionDevice : public IGimbalDevice {
    void move(int, int) override {}
    void stop() override {}
    void home() override {}
};

int main()
{
    ThreadSafeQueue<TrackingResultPtr> input_queue;
    RuntimeControl runtime;
    ControlConfig config;
    config.enabled = true;
    GimbalCommandExecutor executor(config, std::make_unique<NoMotionDevice>(), runtime);
    ControlThread thread(input_queue, &executor, true);
    thread.start();

    auto matched = std::make_shared<TrackingResult>();
    auto matched_frame = std::make_shared<FrameContext>();
    matched_frame->metadata.frame_id = 1;
    matched_frame->metadata.captured_at = std::chrono::steady_clock::now();
    matched->frame = matched_frame;
    matched->tracks.push_back(Track{7});
    matched->enqueued_at = std::chrono::steady_clock::now();
    input_queue.push(matched);

    // 선택 객체가 없는 결과도 최신 입력으로 전달해 상실 처리가 가능해야 한다.
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
