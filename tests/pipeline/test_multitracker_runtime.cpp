#include "pipeline/tracking_thread.hpp"
#include "tracking/botsort_tracker.hpp"
#include "tracking/bytetrack_tracker.hpp"

#include <atomic>
#include <cassert>
#include <memory>

namespace {
class FakeReid final : public IReidPipeline
{
    std::atomic<int>& calls_;
public:
    explicit FakeReid(std::atomic<int>& calls):calls_(calls) {}
    std::vector<float> embed(const cv::Mat&,const cv::Rect& crop) override
    {
        ++calls_;
        return crop.x<50?std::vector<float>{1,0}:std::vector<float>{0,1};
    }
};
DetectionResult frame(int id,std::uint64_t generation,
    std::vector<Detection> detections)
{
    auto context=std::make_shared<FrameContext>();
    context->metadata.frame_id=id;
    context->image=cv::Mat(100,200,CV_16UC1);
    DetectionResult result;
    result.frame=context;result.model_generation=generation;
    result.detections=std::move(detections);
    return result;
}
Detection person(float x) { return {x,5,20,50,0,0.9F,"person"}; }
}
int main()
{
    TrackingConfig byte;
    TrackingConfig bot;bot.type=TrackerType::BotSort;bot.kalman_format="xywh";
    bot.appearance_min_crop_height=1;
    std::map<int,TrackingCatalogEntry> catalog;
    catalog.emplace(1,TrackingCatalogEntry{1,"ByteTrack","",byte});
    catalog.emplace(2,TrackingCatalogEntry{2,"BoT-SORT","",bot});
    std::atomic<int> calls{0};
    RuntimeControl control;
    TrackerManager manager(catalog,1,[&](const TrackingConfig& c)->std::shared_ptr<Tracker> {
        if(c.type==TrackerType::BotSort)
            return std::make_shared<BotSortTracker>(c,std::make_unique<FakeReid>(calls));
        return std::make_shared<ByteTrackTracker>(c);
    },&control.tracker_epoch);
    ThreadSafeQueue<DetectionResult> input;
    ThreadSafeQueue<TrackingResultPtr> output,gui;
    TrackingThread thread(manager,control,input,output,gui,false);
    thread.start();
    input.push(frame(1,0,{person(0)}));
    TrackingResultPtr result;
    assert(output.pop(result) && result->tracks.size()==1 && calls==0);
    const auto old_epoch=result->tracker_epoch;
    manager.activate(2);
    input.push(frame(2,0,{person(0),person(70)}));
    assert(output.pop(result) && result->tracks.size()==2);
    assert(result->tracker_epoch!=old_epoch && calls==2);
    std::uint64_t new_generation=++control.generation;
    input.push(frame(3,new_generation,{person(0)}));
    assert(output.pop(result) && result->model_generation==new_generation);
    assert(result->tracks.size()==1 && result->tracks[0].track_id==1);
    assert(calls==3);
    input.close();thread.join();
    assert(!thread.failed());
    return 0;
}
