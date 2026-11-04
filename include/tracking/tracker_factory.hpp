#pragma once

#include <memory>

#include "config/app_config.hpp"
#include "tracking/tracker.hpp"

// Tracker 구현 선택과 생성을 애플리케이션에서 분리한다.
class TrackerFactory
{
public:
    virtual ~TrackerFactory() = default;

    // 설정에서 선택한 트래커 종류에 맞는 팩토리를 반환한다.
    static std::unique_ptr<TrackerFactory> for_type(TrackerType type);
    // 검증된 추적 설정으로 공통 Tracker 인터페이스 구현체를 만든다.
    virtual std::unique_ptr<Tracker> create(
        const TrackingConfig& config) const = 0;
    // 결과 디렉터리 등에 사용할 트래커 식별 이름을 반환한다.
    virtual const char* name() const noexcept = 0;
};
