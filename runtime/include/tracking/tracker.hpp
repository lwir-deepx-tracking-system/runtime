#pragma once

#include <vector>

#include "common/detection.hpp"
#include "common/track.hpp"


// Tracking 알고리즘이 따라야 하는 공통 인터페이스
class Tracker
{
public:
    virtual ~Tracker() = default;

    // 현재 Frame의 Detection 목록을 받아 Track 목록을 생성한다.
    virtual std::vector<Track> track(
        const std::vector<Detection>& detections
    ) = 0;

    // Tracker가 유지하는 내부 상태를 초기화한다.
    virtual void reset() = 0;
};
