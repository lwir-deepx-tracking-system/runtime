#pragma once

#include <cstdint>
#include <future>
#include <string>
#include "app/pipeline_result.hpp"
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
    virtual PipelineCatalogItem model_info(std::uint32_t) const { return {}; }
    virtual PipelineCatalogItem tracker_info(std::uint32_t) const { return {}; }
    virtual PipelineTrackerPolicy tracker_policy() const { return {}; }
    virtual PipelineChangeTicket request_model_change(std::uint32_t id)   = 0;
    virtual PipelineChangeTicket request_tracker_change(std::uint32_t id) = 0;
    virtual PipelineChangeTicket request_policy_change(const TrackerPolicyUpdate&) {
        return {PipelineResult::NotReady, {}};
    }
};
