#pragma once

#include <cstdint>
#include <future>
#include "gui/gui_protocol.hpp"

// 접수 결과와 실제 전환 완료 future를 분리해 TCP 수신 스레드를 막지 않는다.
struct PipelineChangeTicket {
    GuiPipelineResult result = GuiPipelineResult::NotReady;
    std::shared_future<GuiPipelineResult> completion;
};

// GUI 핸들러가 참조하는 runtime 요청 경계; 구현 객체는 핸들러보다 오래 살아야 한다.
class IPipelineRequests {
public:
    virtual ~IPipelineRequests() = default;
    virtual std::uint32_t model_count() const = 0;
    virtual std::uint32_t tracker_count() const = 0;
    virtual PipelineChangeTicket request_model_change(std::uint32_t id) = 0;
    virtual PipelineChangeTicket request_tracker_change(std::uint32_t id) = 0;
};
