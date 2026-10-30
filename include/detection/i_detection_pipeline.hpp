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

    virtual void wait_idle() {}
};
