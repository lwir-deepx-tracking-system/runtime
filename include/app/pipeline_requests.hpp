#pragma once

#include <cstdint>
#include <future>
#include <string>
#include <vector>
#include "app/pipeline_result.hpp"
#include "detection/npu_feed_state.hpp"
#include "tracking/tracker_policy_update.hpp"

struct PipelineCatalogItem
{
    std::uint32_t id = 0;
    std::string name;
    bool available = false;
    bool active = false;
};

struct PipelineTrackerPolicy
{
    std::uint32_t tracker_id = 0;
    std::uint32_t warmth_min = 0;
    std::uint32_t warmth_max = 0;
    float edge_birth = 0;
    std::uint32_t confirmation_frames = 0;
    std::uint32_t confirmation_ms = 0;
    // bit 0: class gate, bit 1: GMC, bit 2: post ReID.
    std::uint32_t flags = 0;
    std::uint32_t position_memory = 0;
    std::uint32_t box_refine_mode = 0;
    bool box_refine_lost_same = false;
    float box_refine_aim_y = 0.5f;
};

struct PipelineAutoDetection {
    bool enabled = false, available = false;
    std::uint32_t near_id = 0, far_id = 0;
    float far_px = 100.0f, near_px = 140.0f;
};

struct PipelineRecordingStatus {
    bool active = false;
    bool failed = false;
    std::uint32_t session = 0;
    std::uint64_t frames = 0;
    std::uint64_t dropped = 0;
};

struct PipelineRecordingFileChunk {
    bool ok = false;
    std::uint64_t total = 0;
    std::vector<std::uint8_t> bytes;
};

// 접수 결과와 실제 전환 완료 future를 분리해 TCP 수신 스레드를 막지 않는다.
struct PipelineChangeTicket
{
    PipelineResult                     result = PipelineResult::NotReady;
    std::shared_future<PipelineResult> completion;
};

// 콘솔과 GUI가 공유하는 파이프라인 요청 경계; 구현 객체는 핸들러보다 오래 살아야 한다.
class IPipelineRequests
{
  public:
    virtual ~IPipelineRequests()                                          = default;
    virtual std::uint32_t        model_count() const                      = 0;
    virtual std::uint32_t        tracker_count() const                    = 0;
    virtual PipelineCatalogItem model_info(std::uint32_t) const;
    virtual PipelineCatalogItem tracker_info(std::uint32_t) const;
    virtual PipelineTrackerPolicy tracker_policy() const;
    virtual PipelineAutoDetection auto_detection() const;
    virtual bool set_auto_detection(bool);
    virtual bool set_auto_detection_thresholds(float,float);
    virtual NpuFeedState npu_feed_state() const;
    virtual bool set_npu_feed(std::uint8_t);
    virtual bool request_camera_reopen();
    virtual bool recording_available() const;
    virtual PipelineRecordingStatus recording_status() const;
    virtual bool start_recording(const std::string&);
    virtual bool stop_recording();
    virtual std::string latest_recording() const;
    virtual std::vector<std::string> list_recordings() const;
    virtual PipelineRecordingFileChunk read_recording_file(
        const std::string&, std::uint8_t, std::uint64_t, std::uint16_t) const;
    virtual PipelineChangeTicket request_model_change(std::uint32_t id)   = 0;
    virtual PipelineChangeTicket request_tracker_change(std::uint32_t id) = 0;
    virtual PipelineChangeTicket request_policy_change(const TrackerPolicyUpdate&);
};
