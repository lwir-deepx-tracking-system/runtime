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

    // 모든 submit 작업의 완료 콜백이 반환할 때까지 기다린다. 동시 대기 호출을 허용한다.
    // 비동기 구현은 반드시 재정의하며, 기본 구현은 동기 파이프라인에만 해당한다.
    virtual void wait_idle() {}
};
