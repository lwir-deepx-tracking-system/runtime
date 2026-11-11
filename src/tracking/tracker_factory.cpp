#include "tracking/tracker_factory.hpp"

#include <stdexcept>
#include <filesystem>

#include "tracking/bytetrack_tracker.hpp"
#include "tracking/botsort_tracker.hpp"
#include "tracking/post_reid_tracker.hpp"
#include "reid/reid_factory.hpp"

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
class BotSortFactory final : public TrackerFactory
{
public:
    std::unique_ptr<Tracker> create(const TrackingConfig& config) const override
    {
        if(config.type!=TrackerType::BotSort)
            throw std::invalid_argument("TrackerFactory configuration type mismatch");
        return std::make_unique<BotSortTracker>(config);
    }
    const char* name() const noexcept override { return "botsort"; }
};
}  // namespace

// 설정의 TrackerType을 지원하는 구체 팩토리로 변환한다.
std::unique_ptr<TrackerFactory> TrackerFactory::for_type(TrackerType type)
{
    switch (type)
    {
    case TrackerType::ByteTrack:
        return std::make_unique<ByteTrackFactory>();
    case TrackerType::BotSort:
        return std::make_unique<BotSortFactory>();
    }
    throw std::invalid_argument("Unsupported tracker type");
}

std::unique_ptr<Tracker> TrackerFactory::create_configured(
    const TrackingConfig& tracking, const ReidSettings& reid,
    const CameraInputConfig& camera, const ReidBenchmarkConfig& crops)
{
    auto factory=for_type(tracking.type);
    if(tracking.type==TrackerType::ByteTrack && !tracking.post_reid)
        return factory->create(tracking);
    if(reid.model.name.empty() || reid.model.path.empty())
        throw std::invalid_argument("Selected tracker requires a ReID model");
    if(!std::filesystem::is_regular_file(reid.model.path))
        throw std::invalid_argument("Selected tracker ReID model is missing: "+reid.model.path);
    auto reid_factory=ReidFactory::for_model(reid.model);
    auto pipeline=reid_factory->create(reid.model,camera,reid.inference);
    if(tracking.type==TrackerType::BotSort)
        return std::make_unique<BotSortTracker>(tracking,std::move(pipeline),
            crops.person_class_id);
    return std::make_unique<PostReidTracker>(factory->create(tracking),
        std::move(pipeline),reid.matcher,reid.model.output_dimensions,crops);
}
