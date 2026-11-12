#include "tracking/frame_policy_tracker.hpp"
#include "tracking/bytetrack_tracker.hpp"
#include "tracking/botsort_tracker.hpp"
#include "tracking/tracker_factory.hpp"
#include <cassert>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>

namespace {
Detection box(float x, float score=.9F, int cls=0) { return {x,20,20,40,cls,score,{}}; }
struct Observer : Tracker {
    FrameContextPtr frame;
    std::vector<Detection> seen;
    CameraMotion motion;
    int confirmation_frames = 0;
    std::vector<Track> track(const std::vector<Detection>& d) override {seen=d;return {};}
    TrackingStep process(const DetectionResult& d) override {
        frame=d.frame;seen=d.detections;
        TrackingStep result;
        for (std::size_t i=0;i<seen.size();++i) {
            TrackingEmbedding e;e.source_index=i;result.embeddings.push_back(e);
            result.association_events.push_back("1,new,1,"+std::to_string(i));
        }
        result.association_events.push_back("1,lost,1,-1");
        result.association_events.push_back("1,post_reconnect,1,8,0.9");
        return result;
    }
    void reset() override {motion={};}
    void set_camera_motion(const CameraMotion& m) override {motion=m;}
    void set_confirmation_frames(int frames) override {confirmation_frames=frames;}
};
DetectionResult input(cv::Mat image) {
    auto f=std::make_shared<FrameContext>(); f->image=std::move(image);
    DetectionResult d;d.frame=f;return d;
}
}

int main()
{
    cv::Mat sample(4,4,CV_16UC1,cv::Scalar(0));
    sample.at<std::uint16_t>(0,0)=0; sample.at<std::uint16_t>(0,2)=100;
    sample.at<std::uint16_t>(2,0)=200; sample.at<std::uint16_t>(2,2)=300;
    assert(std::abs(warmth_percentile(sample,{0,0,4,4})-285)<1e-6);
    auto observer=std::make_unique<Observer>();auto* observed=observer.get();
    TrackingFrameConfig policy;policy.warmth_min=45000;policy.warmth_max=60000;
    FramePolicyTracker filtered(std::move(observer),policy);
    cv::Mat hot(100,120,CV_16UC1,cv::Scalar(20000));
    hot(cv::Rect(40,20,20,40)).setTo(50000);
    auto d=input(hot);
    d.detections={box(0),box(40),box(80,.9F,2)};
    filtered.process(d);
    assert(observed->frame==d.frame && observed->seen.size()==2 && observed->seen[0].x==40);
    assert(d.detections.size()==3); // Input and image ownership remain unchanged.
    d.detections={box(0),box(40),box(40)}; // Identical boxes must retain distinct source indices.
    const auto indexed=filtered.process(d);
    assert(indexed.embeddings.size()==2);
    assert(indexed.embeddings[0].source_index==1 && indexed.embeddings[1].source_index==2);
    assert(indexed.association_events[0]=="1,new,1,1" && indexed.association_events[1]=="1,new,1,2");
    assert(indexed.association_events[2]=="1,lost,1,-1");
    assert(indexed.association_events[3]=="1,post_reconnect,1,8,0.9");
    bool rejected=false;try {filtered.track(d.detections);} catch(const std::logic_error&) {rejected=true;} assert(rejected);

    TrackingConfig c;c.track_threshold=.25F;c.new_track_threshold=.35F;c.class_gate=true;
    c.match_threshold=1; // Gate must remain closed even at the largest matching threshold.
    ByteTrackTracker classes(c);assert(classes.track({box(10)})[0].track_id==1);
    const auto other=classes.track({box(10,.9F,2)}); assert(other.empty());
    const auto car=classes.track({box(10,.9F,2)});assert(car.size()==1 && car[0].track_id!=1);
    const auto truck=classes.track({box(10,.9F,6)});assert(truck.size()==1 && truck[0].track_id==car[0].track_id);

    c.edge_birth_threshold=.5F;
    ByteTrackTracker edge(c);edge.set_image_size(120,100);
    assert(edge.track({box(0,.4F)}).empty());
    assert(edge.track({box(0,.9F)}).empty());
    const auto confirmed=edge.track({box(0,.9F)});assert(confirmed.size()==1);
    const auto continued=edge.track({box(0,.4F)});assert(continued.size()==1 && continued[0].track_id==confirmed[0].track_id);
    c.edge_birth_threshold=0;c.confirmation_frames=3;
    ByteTrackTracker confirm(c);confirm.track({});
    assert(confirm.track({box(10)}).empty());assert(confirm.track({box(10)}).empty());
    assert(confirm.track({box(10)}).size()==1);
    confirm.reset();assert(confirm.track({box(10)}).size()==1); // Legacy first-frame exception.

    c.type=TrackerType::Sort;c.confirmation_frames=2;
    ByteTrackTracker sort(c);sort.track({box(10)});
    assert(sort.track({box(10,.2F)}).empty()); // SORT does not perform ByteTrack's low pass.

    observer=std::make_unique<Observer>();observed=observer.get();
    policy={};policy.gmc=true;policy.gmc_downscale=1;
    FramePolicyTracker gmc(std::move(observer),policy);
    cv::Mat texture(160,200,CV_16UC1);cv::RNG rng(42);rng.fill(texture,cv::RNG::UNIFORM,0,65535);
    gmc.process(input(texture));assert(observed->motion.is_identity());
    cv::Mat moved;const cv::Mat affine=(cv::Mat_<double>(2,3)<<1,0,5,0,1,3);
    cv::warpAffine(texture,moved,affine,texture.size());gmc.process(input(moved));
    assert(std::abs(observed->motion.tx-5)<.7 && std::abs(observed->motion.ty-3)<.7);
    gmc.reset();gmc.process(input(moved));assert(observed->motion.is_identity());
    gmc.process(input(cv::Mat(80,100,CV_16UC1,cv::Scalar(0))));assert(observed->motion.is_identity());

    // Python reference: 130 ms starts at nominal 25 fps (3 frames), then
    // follows the median of 50 valid capture intervals with hysteresis.
    observer=std::make_unique<Observer>();observed=observer.get();
    policy={};policy.confirmation_ms=130;
    FramePolicyTracker timed(std::move(observer),policy,3);
    assert(observed->confirmation_frames==3);
    assert(timed.current_confirmation_frames()==3);
    for(int i=0;i<=50;++i) {
        auto frame=input(cv::Mat(8,8,CV_16UC1,cv::Scalar(0)));
        auto mutable_frame=std::const_pointer_cast<FrameContext>(frame.frame);
        mutable_frame->metadata.captured_at=std::chrono::steady_clock::time_point(
            std::chrono::seconds(1)+std::chrono::milliseconds(33*i));
        timed.process(frame);
    }
    assert(observed->confirmation_frames==4);
    assert(timed.current_confirmation_frames()==4);
    timed.reset();assert(observed->confirmation_frames==3);
    assert(timed.current_confirmation_frames()==3);
    for(int i=0;i<=50;++i) {
        auto frame=input(cv::Mat(8,8,CV_16UC1,cv::Scalar(0)));
        auto mutable_frame=std::const_pointer_cast<FrameContext>(frame.frame);
        mutable_frame->metadata.captured_at=std::chrono::steady_clock::time_point(
            std::chrono::seconds(1)+std::chrono::milliseconds(50*i));
        timed.process(frame);
    }
    assert(observed->confirmation_frames==3); // Never below the configured base.

    for (const TrackerType type : {TrackerType::ByteTrack, TrackerType::BotSort}) {
        TrackingConfig timed_config;
        timed_config.type=type;
        timed_config.track_threshold=.25F;
        timed_config.new_track_threshold=.35F;
        if(type==TrackerType::BotSort) timed_config.kalman_format="xywh";
        std::unique_ptr<Tracker> base = type==TrackerType::BotSort ?
            std::unique_ptr<Tracker>(std::make_unique<BotSortTracker>(timed_config)) :
            std::unique_ptr<Tracker>(std::make_unique<ByteTrackTracker>(timed_config));
        FramePolicyTracker pipeline(std::move(base),policy,3);
        const auto feed=[&](int i, bool detect) {
            auto frame=input(cv::Mat(80,100,CV_16UC1,cv::Scalar(0)));
            auto mutable_frame=std::const_pointer_cast<FrameContext>(frame.frame);
            mutable_frame->metadata.captured_at=std::chrono::steady_clock::time_point(
                std::chrono::seconds(1)+std::chrono::milliseconds(33*i));
            if(detect) frame.detections={box(10)};
            return pipeline.process(frame).tracks;
        };
        for(int i=0;i<=50;++i) assert(feed(i,false).empty());
        assert(feed(51,true).empty());
        assert(feed(52,true).empty());
        assert(feed(53,true).empty());
        assert(feed(54,true).size()==1);
    }

    for(const char* key:{"warmth_min: -1","warmth_max: 70000","gmc_downscale: 0","confirmation_frames: 0","confirmation_ms: 1001","edge_birth_threshold: 2"}) {
        rejected=false;
        try {config_parser::load_tracking_config(YAML::Load(std::string("track_threshold: .5\nmatch_threshold: .8\ntrack_buffer: 30\n")+key));}
        catch(const std::runtime_error&) {rejected=true;} assert(rejected);
    }
    const auto oc=load_tracker_config("config/tracking/ocsort.yaml");
    assert(oc.type==TrackerType::OcSort && oc.oru && oc.ocr && oc.ocm_weight>0);
    assert(std::string(TrackerFactory::for_type(oc.type)->name())=="ocsort");
}
