#include "tracking/tracker_factory.hpp"
#include "tracking/bytetrack_tracker.hpp"

#include <stdexcept>

// 트래커 팩토리 생성과 지원하지 않는 종류의 거부를 확인한다.
int main()
{
    TrackingConfig config;
    auto factory = TrackerFactory::for_type(config.type);
    if (!factory || std::string(factory->name()) != "bytetrack") return 1;

    std::unique_ptr<Tracker> tracker = factory->create(config);
    if (!tracker || dynamic_cast<ByteTrackTracker*>(tracker.get()) == nullptr)
        return 2;

    try {
        TrackerFactory::for_type(static_cast<TrackerType>(255));
        return 3;
    } catch (const std::invalid_argument&) {}

    config.type = static_cast<TrackerType>(255);
    try {
        factory->create(config);
        return 4;
    } catch (const std::invalid_argument&) {}
    return 0;
}
