#pragma once

#include <vector>

#include "common/detection.hpp"
#include "common/frame.hpp"

class IDetectionPipeline
{
public:
    virtual ~IDetectionPipeline() = default;
    virtual std::vector<Detection> detect(const FrameContext& frame) = 0;
};
