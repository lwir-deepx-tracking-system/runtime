#include "tracking/tracker_factory.hpp"
#include "tracking/ocsort_tracker.hpp"

#include <stdexcept>

#include "tracking/bytetrack_tracker.hpp"

namespace
{
// ByteTrack 구현체의 생성과 이름을 TrackerFactory 계약에 맞춰 제공한다.
class ByteTrackFactory final : public TrackerFactory
{
public:
    std::unique_ptr<Tracker> create(
        const TrackingConfig& config) const override
    {
        if (config.type != TrackerType::ByteTrack)
            throw std::invalid_argument("TrackerFactory configuration type mismatch");
        return std::make_unique<ByteTrackTracker>(config);
    }

    const char* name() const noexcept override { return "bytetrack"; }
};

class OCSortFactory final : public TrackerFactory
{
public:
    std::unique_ptr<Tracker> create(
        const TrackingConfig& config) const override
    {
        if (config.type != TrackerType::OCSort)
            throw std::invalid_argument("TrackerFactory configuration type mismatch");
        return std::make_unique<OCSortTracker>(config.ocsort);
    }
    const char* name() const noexcept override { return "ocsort"; }
};
}  // namespace

// 설정의 TrackerType을 지원하는 구체 팩토리로 변환한다.
std::unique_ptr<TrackerFactory> TrackerFactory::for_type(TrackerType type)
{
    switch (type)
    {
    case TrackerType::ByteTrack:
        return std::make_unique<ByteTrackFactory>();
    case TrackerType::OCSort:
        return std::make_unique<OCSortFactory>();
    }
    throw std::invalid_argument("Unsupported tracker type");
}