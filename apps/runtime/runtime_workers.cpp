#include "runtime_workers.hpp"
#include "pipeline/tracker_change_service.hpp"

#include "pipeline/camera_thread.hpp"
#include "pipeline/detection_thread.hpp"
#include "pipeline/tracking_thread.hpp"
#include "pipeline/control_thread.hpp"
#include "pipeline/gui_sender_thread.hpp"
#include "pipeline/gui_receiver_thread.hpp"
#include "control/gimbal_command_executor.hpp"

// RuntimeApplication에서 전달한 worker와 큐 참조로 실행 순서를 구성한다.
RuntimeWorkers::RuntimeWorkers(RuntimeControl& runtime,
    ThreadSafeQueue<FrameMessage>& frames,
    ThreadSafeQueue<ModelChangeRequest>& model_requests,
    CameraThread& camera, DetectionThread& detection,
    ModelManagerThread& model_manager, TrackingThread& tracking,
    ControlThread& control, GimbalCommandExecutor* gimbal,
    GuiSenderThread* gui_sender, GuiReceiverThread* gui_receiver,
    TrackerChangeService& tracker_changes)
    : tracker_changes_(tracker_changes), runtime_(runtime), frames_(frames), model_requests_(model_requests),
      camera_(camera), detection_(detection), model_manager_(model_manager),
      tracking_(tracking), control_(control), gimbal_(gimbal),
      gui_sender_(gui_sender), gui_receiver_(gui_receiver)
{
}

void RuntimeWorkers::start()
{
    model_manager_.start();
    model_manager_.wait_until_initialized();
    tracker_changes_.start();
    if (gui_receiver_) gui_receiver_->start();
    if (gui_sender_) gui_sender_->start();
    control_.start();
    tracking_.start();
    detection_.start();
    camera_.start();
}

void RuntimeWorkers::stop()
{
    runtime_.request_stop();
    tracker_changes_.close();
    if (gimbal_) gimbal_->shutdown();
    camera_.request_stop();
    frames_.close();
    model_requests_.close();
    if (gui_receiver_) gui_receiver_->stop();
}

void RuntimeWorkers::join()
{
    // 콜백과 활성 참조 교체를 끝낸 뒤 짐벌·추적 worker를 정리한다.
    tracker_changes_.close();
    tracker_changes_.join();
    if (gimbal_) gimbal_->shutdown();
    camera_.join();
    detection_.join();
    // No further model change is possible after detection has drained.
    model_requests_.close();
    model_manager_.join();
    tracking_.join();
    if (gui_sender_) gui_sender_->join();
    control_.join();
    if (gui_receiver_) {
        gui_receiver_->stop();
        gui_receiver_->join();
    }
}
