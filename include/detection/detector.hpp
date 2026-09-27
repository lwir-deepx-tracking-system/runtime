#pragma once

#include <vector>

#include "common/detection.hpp"
#include "common/frame.hpp"

// DX 전처리, NPU 추론, 후처리를 하나의 Detection 단계로 감싸는 인터페이스.
class Detector
{
public:
    virtual ~Detector() = default;

    virtual std::vector<Detection> detect(const FrameContext& frame) = 0;
};
