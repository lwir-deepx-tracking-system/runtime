#include "detection/auto_model_selector.hpp"

#include <chrono>
#include <memory>

int main() {
    using Clock = std::chrono::steady_clock;
    AutoModelSelector chooser({12, 13});
    const auto start = Clock::now();
    auto step = [&](double s, int model, float height, bool predicted = false) {
        DetectionResult d;
        auto frame = std::make_shared<FrameContext>();
        frame->metadata.captured_at = start + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(s));
        frame->image = cv::Mat(480, 640, CV_16UC1);
        d.frame = frame;
        TrackingResult r;
        if (height > 0) {
            Track t;
            t.class_id = 0; t.x = 100; t.y = 100; t.width = 40;
            t.height = height; t.confidence = 0.9F; t.predicted = predicted;
            r.tracks.push_back(t);
        }
        return chooser.observe(d, r, model);
    };
    if (step(0, 12, 80) || step(0.5, 12, 80)) return 1;
    if (step(1.1, 12, 80) != 13) return 2;
    if (step(1.2, 12, 80)) return 3; // no duplicate request
    if (step(1.3, 13, 80)) return 4; // switch acknowledgement
    if (step(2.0, 13, 180) || step(3.1, 13, 180)) return 5; // dwell
    if (step(6.4, 13, 180) != 12) return 6;
    if (step(6.5, 12, 180)) return 7;
    if (step(6.6, 12, 0, true)) return 8;
    if (step(7.2, 12, 0, true) != 13) return 19;

    AutoModelSelector::Config recovery_config{12, 13};
    recovery_config.hold_s = recovery_config.dwell_s = 0;
    AutoModelSelector recovery(recovery_config);
    auto check = [&](double s, int model, float height, bool edge = false,
                     float confidence = 0.9F) {
        DetectionResult d;
        auto frame = std::make_shared<FrameContext>();
        frame->metadata.captured_at = start + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(s));
        frame->image = cv::Mat(480, 640, CV_16UC1);
        d.frame = frame;
        TrackingResult r;
        if (height > 0) {
            Track t;
            t.class_id = 0; t.x = edge ? 0 : 100; t.y = 100;
            t.width = 40; t.height = height; t.confidence = confidence;
            r.tracks.push_back(t);
        }
        return recovery.observe(d, r, model);
    };
    if (check(0, 13, 180) != 12) return 9;
    if (check(0.1, 12, 0) || check(0.7, 12, 0) != 13) return 10;
    if (check(1.0, 13, 180) || check(30.6, 13, 180)) return 11;
    if (check(30.8, 13, 180) != 12) return 12;
    if (check(31, 12, 80, true)) return 13; // small but cropped: keep near
    if (check(31.1, 12, 80, false) != 13) return 14;
    if (check(31.2, 13, 5000)) return 15; // implausible far-model box
    if (check(31.3, 13, 180, false, 0.1F)) return 16; // weak track

    AutoModelSelector::Config motion_config{12, 13};
    motion_config.dwell_s = 0;
    AutoModelSelector motion(motion_config);
    auto moving = [&](double s) {
        DetectionResult d;
        auto frame = std::make_shared<FrameContext>();
        frame->metadata.captured_at = start + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(s));
        frame->image = cv::Mat(480, 640, CV_16UC1);
        d.frame = frame;
        TrackingResult r;
        Track t;
        t.track_id = 7; t.class_id = 0; t.confidence = 0.9F;
        t.x = static_cast<float>(100 + s * 100); t.y = 100;
        t.width = 40; t.height = 80;
        r.tracks.push_back(t);
        return motion.observe(d, r, 12, 7);
    };
    for (double s : {0.0, 0.5, 1.1, 1.6, 2.1, 2.6, 3.1, 3.6})
        if (moving(s)) return 17; // fast movement delays the size-based switch
    if (moving(4.2) != 13) return 18; // bounded three-second deferral

    AutoModelSelector::Config followed_config{12, 13};
    followed_config.hold_s=followed_config.dwell_s=0;
    AutoModelSelector followed(followed_config);
    DetectionResult input;
    auto selected_frame=std::make_shared<FrameContext>();
    selected_frame->metadata.captured_at=start;
    selected_frame->image=cv::Mat(480,640,CV_16UC1);
    input.frame=selected_frame;
    TrackingResult selected_tracks;
    Track near;
    near.track_id=5;near.class_id=0;near.confidence=.9F;
    near.x=100;near.y=100;near.width=40;near.height=180;
    Track far=near;far.track_id=7;far.height=80;
    selected_tracks.tracks={near,far};
    if (followed.observe(input,selected_tracks,12,7)!=13) return 20;
    selected_tracks.tracks={near};
    if (followed.observe(input,selected_tracks,13,7)) return 21; // selected target missing

    AutoModelSelector scene(followed_config);
    Track edge=far;edge.x=0;
    selected_tracks.tracks={near,edge};
    if (scene.observe(input,selected_tracks,12)) return 22; // clean box wins over cut box
    return 0;
}
