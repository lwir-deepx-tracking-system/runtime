#include "tracking/box_refine_tracker.hpp"

#include <cassert>
#include <cmath>

namespace {
struct FakeTracker : Tracker {
    std::vector<Track> output;
    std::vector<Track> track(const std::vector<Detection>&) override { return output; }
    TrackingStep process(const DetectionResult&) override {
        TrackingStep step;
        step.tracks=output;
        step.raw_tracks=output;
        return step;
    }
    void reset() override {}
};
DetectionResult frame(int id) {
    DetectionResult result;
    auto context=std::make_shared<FrameContext>();
    context->metadata.frame_id=id;
    context->metadata.captured_at=std::chrono::steady_clock::time_point(std::chrono::milliseconds(1000+id*33));
    result.frame=context;
    return result;
}
Track box(float width,bool predicted=false) {
    Track t;t.track_id=7;t.x=100-width/2;t.y=50-width/4;
    t.width=width;t.height=width/2;t.class_id=0;t.predicted=predicted;
    return t;
}
bool close(double x,double y) {return std::abs(x-y)<1e-4;}
}

int main() {
    auto fake=std::make_unique<FakeTracker>();
    auto* source=fake.get();
    BoxRefineConfig config;config.mode="size";config.lost_same=true;config.aim_y=.3;
    BoxRefineTracker tracker(std::move(fake),config);
    source->output={box(20)};
    auto first=tracker.process(frame(1));
    assert(close(first.tracks[0].width,20));
    source->output={box(40)};
    auto second=tracker.process(frame(2));
    assert(second.tracks[0].width>20 && second.tracks[0].width<40);
    assert(close(second.raw_tracks[0].width,40));
    assert(close(second.unrefined_tracks[0].width,40));
    assert(close(second.tracks[0].x+second.tracks[0].width/2,100));
    assert(close(second.tracks[0].y+.3*second.tracks[0].height,source->output[0].y+.3*source->output[0].height));
    source->output={box(60,true)};
    auto lost=tracker.process(frame(3));
    assert(close(lost.tracks[0].width,second.tracks[0].width));
    assert(lost.tracks[0].predicted);
    tracker.reset();
    auto reset=tracker.process(frame(4));
    assert(close(reset.tracks[0].width,60)); // No prior size after reset.
    source->output={box(20)};
    assert(close(tracker.process(frame(5)).tracks[0].width,20));
    CameraMotion zoom;zoom.a=2;zoom.d=2;
    tracker.set_camera_motion(zoom);
    source->output={box(40)};
    assert(close(tracker.process(frame(6)).tracks[0].width,40));

    auto plain=std::make_unique<FakeTracker>();auto* plain_source=plain.get();
    config.lost_same=false;
    BoxRefineTracker no_hold(std::move(plain),config);
    plain_source->output={box(20)};no_hold.process(frame(1));
    plain_source->output={box(50,true)};
    assert(close(no_hold.process(frame(2)).tracks[0].width,50));

    auto body_base=std::make_unique<FakeTracker>();auto* body_source=body_base.get();
    config.mode="body";
    BoxRefineTracker body(std::move(body_base),config);
    Track person=box(40);person.height=80;person.y=60;
    body_source->output={person};
    auto person_input=frame(1);
    person_input.detections={{person.x,person.y,person.width,person.height,0,.9F,{}}};
    const auto converted=body.process(person_input);
    assert(std::abs(converted.tracks[0].width-80*0.33128652)<.001);
    assert(std::abs(converted.tracks[0].height-80*0.53458081)<.001);
    assert(close(converted.raw_tracks[0].height,80));
    body.reset();
    auto pose_input=frame(2);
    Detection pose{person.x,person.y,person.width,person.height,0,.9F,{}};
    pose.keypoints.resize(17);
    pose_input.detections={pose};
    const auto pose_box=body.process(pose_input);
    assert(std::abs(pose_box.tracks[0].width-27.9412107245)<.001);
    assert(std::abs(pose_box.tracks[0].height-42.1465373391)<.001);
    body_source->output={box(50,true)};
    assert(close(body.process(frame(3)).tracks[0].width,50));

    body.reset();
    body_source->output={person};
    auto before=body.process(pose_input);
    body.reset_for_model_change();
    person_input.frame=frame(4).frame;
    auto after=body.process(person_input);
    const auto aim=[](const Track& t) {return t.y+.3*t.height;};
    assert(std::abs(aim(after.tracks[0])-aim(before.tracks[0]))<.001);

    tracker.reset();source->output={box(20)};tracker.process(frame(1));
    Track changed_id=box(28);changed_id.track_id=8;
    source->output={changed_id};
    auto handed=tracker.process(frame(2));
    assert(handed.tracks[0].width>20 && handed.tracks[0].width<28);
}
