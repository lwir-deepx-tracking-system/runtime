#include "tracking/post_reid_tracker.hpp"
#include "tracking/bytetrack_tracker.hpp"

#include <cassert>
#include <memory>

namespace {
class FakeReid final : public IReidPipeline
{
public:
    int calls=0;
    std::vector<float> embed(const cv::Mat&,const cv::Rect&) override
    {
        ++calls;return {1,0};
    }
};
DetectionResult input(std::uint64_t frame_id,std::vector<Detection> detections)
{
    auto frame=std::make_shared<FrameContext>();
    frame->metadata.frame_id=frame_id;
    frame->image=cv::Mat(100,200,CV_16UC1);
    DetectionResult result;result.frame=frame;result.detections=std::move(detections);
    return result;
}
Detection person(float x) { return {x,5,20,50,0,0.9F,"person"}; }
}
int main()
{
    TrackingConfig c;
    auto plain=ByteTrackTracker(c);
    assert(plain.process(input(1,{person(0)})).embeddings.empty());
    auto fake=std::make_unique<FakeReid>();auto* raw=fake.get();
    ReidMatcherConfig matcher;
    ReidBenchmarkConfig crops{0,1,2};
    PostReidTracker post(std::make_unique<ByteTrackTracker>(c),
        std::move(fake),matcher,2,crops);
    auto first=post.process(input(1,{person(0)}));
    assert(first.tracks.size()==1 && first.raw_tracks.size()==1);
    assert(raw->calls==1 && first.embeddings.size()==1);
    assert(!first.association_events.empty()); // no prior gallery is diagnosed
    post.process(input(2,{}));
    post.process(input(3,{person(120)})); // new ByteTrack ID is unconfirmed
    auto new_person=post.process(input(4,{person(120)}));
    assert(new_person.tracks.size()==1 && raw->calls==2);
    assert(new_person.tracks[0].track_id==first.tracks[0].track_id);
    assert(new_person.raw_tracks[0].track_id!=first.raw_tracks[0].track_id);
    post.reset();
    auto reset=post.process(input(1,{person(0)}));
    assert(reset.tracks.size()==1 && raw->calls==3);
    return 0;
}
