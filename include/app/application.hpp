#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/frame.hpp"
#include "common/threadsafequeue.hpp"
#include "common/detection.hpp"
#include "common/track.hpp"

#include "camera/camera.hpp"
#include "detection/dxapp_detection_pipeline.hpp"
#include "gui/gui_receiver.hpp"
#include "gui/gui_sender.hpp"
#include "tracking/tracker.hpp"

#include "pipeline/camera_thread.hpp"
#include "pipeline/detection_thread.hpp"
#include "pipeline/tracking_thread.hpp"
#include "pipeline/target_selection_thread.hpp"
#include "pipeline/control_thread.hpp"
#include "pipeline/gui_receiver_thread.hpp"
#include "pipeline/gui_sender_thread.hpp"

// 프로그램의 구성 요소를 생성하고
// Pipeline 실행을 관리하는 최상위 Application 클래스
class Application
{
private:
    // Camera의 shared FrameContext를 Detection으로 전달
    ThreadSafeQueue<FrameMessage> frame_queue_;

    // Detection 목록 다음 Stage로 전달
    ThreadSafeQueue<DetectionResult> detection_queue_;

    // 같은 TrackingResult를 제어와 GUI 경로가 공유한다.
    ThreadSafeQueue<TrackingResultPtr> control_track_queue_;
    ThreadSafeQueue<TrackingResultPtr> gui_track_queue_;

    // 선택 ID의 관측 결과를 Orange Pi 짐벌 제어 단계로 전달
    ThreadSafeQueue<TargetSelection> target_selection_queue_;

    std::string config_path_;
    std::vector<std::string> config_snapshot_paths_;
    bool measurement_enabled_ = false;

/************************************************************************/
    // Camera 객체
    std::unique_ptr<Camera> camera_;

    // Camera pthread
    std::unique_ptr<CameraThread> camera_thread_;

    // DX 전처리, 추론, 후처리를 소유하는 통합 Detection 단계
    std::unique_ptr<DetectionPipeline> detection_pipeline_;
    std::unique_ptr<DetectionThread> detection_thread_;

    // 현재 사용하는 Tracker 객체를 Application이 소유한다.
    std::unique_ptr<Tracker> tracker_;

    // Tracking pthread
    std::unique_ptr<TrackingThread> tracking_thread_;

    TargetSelector target_selector_;
    std::unique_ptr<TargetSelectionThread> target_selection_thread_;

    GimbalController gimbal_controller_;
    std::unique_ptr<ControlThread> control_thread_;

    // Application이 GUI 통신 객체의 전체 수명을 소유한다. Sender 내부의
    // GStreamer pipeline과 Receiver 내부의 TCP socket은 각 객체가 RAII로
    // 정리하고, worker는 실행 thread만 담당한다.
    std::unique_ptr<GuiSender> gui_sender_;
    std::unique_ptr<GuiSenderThread> gui_sender_thread_;
    std::unique_ptr<GuiReceiver> gui_receiver_;
    std::unique_ptr<GuiReceiverThread> gui_receiver_thread_;


public:
    // YAML 설정을 읽고 필요한 객체를 생성
    explicit Application(const std::string& config_path);

    // GUI가 선택한 Track ID를 전달한다. -1은 선택 해제.
    void set_selected_track_id(int track_id);

    // GUI 송신 worker가 최신 TrackingResult를 가져가는 경계.
    // queue가 닫히고 남은 결과가 없으면 false를 반환한다.
    bool pop_gui_result(TrackingResultPtr& result);

    // Pipeline Worker 실행
    void run();
};
