#include "tracking/botsort_tracker.hpp"

#include <cassert>
#include <memory>
#include <set>
#include <stdexcept>

namespace {
Detection person(float x, float confidence=0.9F)
{
    return {x,10,20,40,0,confidence,"person"};
}
TrackingConfig config()
{
    TrackingConfig c;
    c.type=TrackerType::BotSort;c.kalman_format="xywh";
    c.track_threshold=0.5F;c.match_threshold=0.8F;c.track_buffer=3;
    c.proximity_threshold=0.8F;c.appearance_threshold=0.25F;
    c.appearance_min_crop_height=1;
    return c;
}
int id_at(const TrackingStep& step, float x)
{
    for(const auto& t:step.tracks) if(t.x==x) return t.track_id;
    return -1;
}
class FakeReid : public IReidPipeline
{
public:
    int calls=0;
    std::vector<float> embed(const cv::Mat&,const cv::Rect& crop) override
    {
        ++calls;
        return crop.x<50?std::vector<float>{1,0}:std::vector<float>{0,1};
    }
};
}
int main()
{
    auto c=config();
    BotSortTracker bot(c);
    auto first=bot.track_with_features({person(0),person(20)},{{1,0},{0,1}});
    assert(first.tracks.size()==2);
    const int a=id_at(first,0), b=id_at(first,20);
    assert(a>0&&b>0&&a!=b);
    // Motion prefers the crossed assignment; appearance prefers the original IDs.
    BotSortTracker motion_only(c);
    motion_only.track({person(0),person(20)});
    const auto motion_crossed=motion_only.track_with_features(
        {person(12),person(8)},{{},{}});
    assert(id_at(motion_crossed,12)==b && id_at(motion_crossed,8)==a);
    auto crossed=bot.track_with_features({person(12),person(8)},{{1,0},{0,1}});
    assert(id_at(crossed,12)==a && id_at(crossed,8)==b);
    std::set<int> ids;
    for(const auto& t:crossed.tracks) assert(ids.insert(t.track_id).second);
    bot.track_with_features({},{});
    auto recovered=bot.track_with_features({person(13)},{{1,0}});
    assert(id_at(recovered,13)==a);
    bot.reset();
    auto restarted=bot.track_with_features({person(0)},{{1,0}});
    assert(restarted.tracks.size()==1 && restarted.tracks[0].track_id==1);
    bool rejected=false;
    try { bot.track_with_features({person(0)},{}); }
    catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    BotSortTracker fallback(c);
    fallback.track({person(0)});
    assert(fallback.track({person(2)}).front().track_id==1);
    auto fake=std::make_unique<FakeReid>();
    auto* raw=fake.get();
    BotSortTracker with_reid(c,std::move(fake));
    auto frame=std::make_shared<FrameContext>();
    frame->image=cv::Mat(80,200,CV_16UC1);
    DetectionResult input;input.frame=frame;
    input.detections={person(0),person(60),person(120)};
    auto output=with_reid.process(input);
    assert(raw->calls==3 && output.embeddings.size()==3);
    assert(output.embeddings[0].source_index==0 &&
        output.embeddings[1].source_index==1 && output.embeddings[2].source_index==2);
    c.appearance_max_crops_per_frame=2;
    auto limited=std::make_unique<FakeReid>();auto* limited_raw=limited.get();
    BotSortTracker capped(c,std::move(limited));
    assert(capped.process(input).embeddings.size()==2 && limited_raw->calls==2);
    return 0;
}
