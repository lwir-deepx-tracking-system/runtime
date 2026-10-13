#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <vector>

#include "common/detection.hpp"
#include "common/frame.hpp"

class IDetectionPipeline
{
public:
    using Completion =
        std::function<void(std::vector<Detection>, std::exception_ptr)>;

    virtual ~IDetectionPipeline() = default;
    virtual std::vector<Detection> detect(const FrameContext& frame) = 0;

    virtual void submit_async(
        const std::shared_ptr<FrameContext>& frame,
        Completion completion)
    {
        std::vector<Detection> detections;
        std::exception_ptr error;
        try
        {
            detections = detect(*frame);
        }
        catch (...)
        {
            error = std::current_exception();
        }
        completion(std::move(detections), error);
    }

    virtual void wait_idle() {}
};
