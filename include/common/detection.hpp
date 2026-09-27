#pragma once

#include <chrono>
#include <vector>

#include "common/frame.hpp"

// 한 객체의 검출 결과.
struct Detection
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    int class_id = -1;
    float confidence = 0.0f;
};

// 통합 Detection 단계의 출력. 원본 영상은 복사하지 않는다.
struct DetectionResult
{
    FrameContextPtr frame;
    std::vector<Detection> detections;
    std::chrono::steady_clock::time_point enqueued_at{};
};
