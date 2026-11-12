#pragma once

#include <memory>

#include "reid/i_reid_pipeline.hpp"
#include "reid/reid_matcher.hpp"
#include "tracking/tracker.hpp"

// Owns ByteTrack, RepVGG and the stable-ID gallery in one tracker generation.
class PostReidTracker final : public Tracker
{
public:
    PostReidTracker(std::unique_ptr<Tracker> base,
        std::unique_ptr<IReidPipeline> pipeline,
        ReidMatcherConfig matcher_config, int dimensions,
        ReidBenchmarkConfig crop_config);
    std::vector<Track> track(const std::vector<Detection>& detections) override;
    TrackingStep process(const DetectionResult& input) override;
    void reset() override;
    void set_confirmation_frames(int frames) override { base_->set_confirmation_frames(frames); }
    void set_camera_motion(const CameraMotion& motion) override { base_->set_camera_motion(motion); }
    void set_image_size(float width, float height) override { base_->set_image_size(width, height); }
private:
    std::unique_ptr<Tracker> base_;
    std::unique_ptr<IReidPipeline> pipeline_;
    ReidMatcher matcher_;
    ReidBenchmarkConfig crop_;
};
