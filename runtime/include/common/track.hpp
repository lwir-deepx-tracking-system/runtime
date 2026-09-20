#pragma once

#include <vector>

#include "common/frame_metadata.hpp"


// Tracker가 생성한 객체의 Tracking 결과를 저장한다.
struct Track
{
    // Tracker가 객체에 부여한 고유 ID
    int track_id = -1;

    // Tracking된 객체의 Bounding Box
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    // 객체 Class
    int class_id = -1;

    // Tracking 결과의 신뢰도
    float confidence = 0.0f;
};

// 한 프레임의 추적 목록과 프레임 정보를 함께 전달한다.
struct TrackingResult
{
    FrameMetadata metadata;
    std::vector<Track> tracks;
};
