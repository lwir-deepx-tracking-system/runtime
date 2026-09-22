#pragma once

#include <vector>

#include "common/frame_metadata.hpp"

// 한 객체의 검출 결과. 필드는 후처리 구현 시 추가한다.
struct Detection
{
};

// 한 프레임의 검출 목록과 프레임 정보를 함께 전달한다.
struct DetectionResult
{
    FrameMetadata metadata;
    std::vector<Detection> detections;
};
