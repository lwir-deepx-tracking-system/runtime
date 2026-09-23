#pragma once

#include <memory>
#include <string>

#include "common/frame.hpp"
#include "common/threadsafequeue.hpp"
#include "common/detection.hpp"
#include "common/track.hpp"

#include "camera/camera.hpp"
#include "detection/dxapp_detection_pipeline.hpp"
#include "tracking/tracker.hpp"

#include "pipeline/camera_thread.hpp"
#include "pipeline/detection_thread.hpp"
#include "pipeline/tracking_thread.hpp"
#include "pipeline/target_selection_thread.hpp"
#include "pipeline/control_thread.hpp"

// 프로그램의 구성 요소를 생성하고
// Pipeline 실행을 관리하는 최상위 Application 클래스
class Application
{
private:
    // Stage 사이에서 Frame 전달
    ThreadSafeQueue<Frame> frame_queue_;

    // Detection 목록 다음 Stage로 전달
    ThreadSafeQueue<DetectionResult> detection_queue_;

    // Tracking 결과를 다음 Stage로 전달
    ThreadSafeQueue<TrackingResult> track_queue_;

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


public:
    // YAML 설정을 읽고 필요한 객체를 생성
    explicit Application(const std::string& config_path);

    // GUI가 선택한 Track ID를 전달한다. -1은 선택 해제.
    void set_selected_track_id(int track_id);

    // Pipeline Worker 실행
    void run();
};
