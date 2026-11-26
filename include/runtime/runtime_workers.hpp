#pragma once

#include "common/threadsafequeue.hpp"
#include "common/frame.hpp"
#include "pipeline/model_manager_thread.hpp"
#include "pipeline/runtime_control.hpp"

class TrackerChangeService;
class CameraThread;
class DetectionThread;
class TrackingThread;
class ControlThread;
class GuiSenderThread;
class GuiReceiverThread;
class GimbalCommandExecutor;

// 한 번의 Runtime 실행에서 worker의 시작·중지·종료 대기 순서를 관리한다.
class RuntimeWorkers
{
    TrackerChangeService& tracker_changes_;
    RuntimeControl& runtime_;
    ThreadSafeQueue<FrameMessage>& frames_;
    ThreadSafeQueue<ModelChangeRequest>& model_requests_;
    CameraThread& camera_;
    DetectionThread& detection_;
    ModelManagerThread& model_manager_;
    TrackingThread& tracking_;
    ControlThread& control_;
    GimbalCommandExecutor* gimbal_;
    GuiSenderThread* gui_sender_;
    GuiReceiverThread* gui_receiver_;

public:
    // Application이 소유한 worker와 큐의 참조를 받아 실행 순서를 구성한다.
    RuntimeWorkers(RuntimeControl& runtime,
        ThreadSafeQueue<FrameMessage>& frames,
        ThreadSafeQueue<ModelChangeRequest>& model_requests,
        CameraThread& camera, DetectionThread& detection,
        ModelManagerThread& model_manager, TrackingThread& tracking,
        ControlThread& control, GimbalCommandExecutor* gimbal,
        GuiSenderThread* gui_sender, GuiReceiverThread* gui_receiver,
        TrackerChangeService& tracker_changes);
    // 모델 초기화 뒤 소비 worker를 시작하고 카메라 생산자를 마지막에 시작한다.
    void start();
    // 종료 상태를 알리고 입력 큐를 닫아 대기 중인 worker를 깨운다.
    void stop();
    // 남은 입력을 처리한 뒤 worker들을 의존 순서대로 합류시킨다.
    void join();
};
