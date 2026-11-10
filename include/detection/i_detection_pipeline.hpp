#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <vector>

#include "common/detection_timing.hpp"
#include "common/detection.hpp"
#include "common/frame.hpp"

class IDetectionPipeline
{
public:
    using Completion =
        std::function<void(
            std::vector<Detection>, std::exception_ptr, DetectionTiming)>;

    virtual ~IDetectionPipeline() = default;
    virtual void submit_async(
        const FrameContextPtr& frame,
        Completion completion) = 0;

    // 신규 제출을 멈춘 호출자가 모든 작업의 콜백 반환까지 기다리는 합류 경계다.
    // 추론·후처리 오류는 Completion의 exception_ptr로 보고하고 대기는 계속한다.
    // 동시 대기자를 허용하며, 종료를 확인하지 못한 채 반환하거나 예외를 내보내면 안 된다.
    virtual void wait_idle() noexcept = 0;
};
