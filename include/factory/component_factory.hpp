#pragma once

#include <memory>

#include "app/app_config.hpp"
#include "detection/detector.hpp"
#include "tracking/tracker.hpp"

class ComponentFactory
{
public:
    
    // 전처리, DX 추론, 후처리를 소유하는 통합 Detection 객체 생성
    static std::unique_ptr<Detector> create_detector(const AppConfig& config);

    // Tracking 객체 생성
    static std::unique_ptr<Tracker> create_tracker(const AppConfig& config);
};
