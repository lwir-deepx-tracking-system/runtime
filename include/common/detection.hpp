#pragma once

#include <string>
#include <vector>

#include "common/frame_metadata.hpp"

struct Detection
{
    float x1 = 0.0F;
    float y1 = 0.0F;
    float x2 = 0.0F;
    float y2 = 0.0F;
    float confidence = 0.0F;
    int class_id = 0;
    std::string class_name;
};

// 한 프레임의 검출 목록과 프레임 정보를 함께 전달한다.
struct DetectionResult
{
    FrameMetadata metadata;
    std::vector<Detection> detections;
};
