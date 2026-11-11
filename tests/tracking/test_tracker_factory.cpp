#include "tracking/tracker_factory.hpp"
#include "tracking/bytetrack_tracker.hpp"
#include "tracking/botsort_tracker.hpp"

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
    ReidSettings reid;
    CameraInputConfig camera;
    ReidBenchmarkConfig crops;
    if(dynamic_cast<ByteTrackTracker*>(TrackerFactory::create_configured(
        config,reid,camera,crops).get())==nullptr) return 5;
    config.type=TrackerType::BotSort;
    config.kalman_format="xywh";
    auto bot_factory=TrackerFactory::for_type(config.type);
    if(std::string(bot_factory->name())!="botsort" ||
        dynamic_cast<BotSortTracker*>(bot_factory->create(config).get())==nullptr)
        return 6;
    try { TrackerFactory::create_configured(config,reid,camera,crops);return 7; }
    catch(const std::invalid_argument&) {}

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
