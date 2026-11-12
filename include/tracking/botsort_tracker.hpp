#pragma once

#include <memory>
#include <vector>

#include "config/app_config.hpp"
#include "reid/i_reid_pipeline.hpp"
#include "tracking/kalman_filter.hpp"
#include "tracking/tracker.hpp"

// BoT-SORT association with XYWH Kalman motion and smoothed appearance features.
// Frame-aware policy wrapper supplies camera motion when configured.
class BotSortTracker final : public Tracker
{
public:
    explicit BotSortTracker(TrackingConfig config,
        std::unique_ptr<IReidPipeline> reid = {}, int person_class_id = 0);
    std::vector<Track> track(const std::vector<Detection>& detections) override;
    TrackingStep process(const DetectionResult& input) override;
    // CPU tests can supply normalized detection-indexed features without DXRT.
    TrackingStep track_with_features(const std::vector<Detection>& detections,
        const std::vector<std::vector<float>>& features);
    void reset() override;
    void set_confirmation_frames(int frames) override { config_.confirmation_frames = frames; }
    void set_camera_motion(const CameraMotion& motion) override { motion_ = motion; }
    void set_image_size(float width, float height) override { width_ = width; height_ = height; }

private:
    enum class State { Tracked, Lost, Removed };
    struct Target {
        int id = 0;
        int last_frame = 0;
        State state = State::Tracked;
        bool confirmed = false;
        int hits = 1;
        KalmanState kf{};
        Detection observation;
        std::vector<float> feature;
    };
    TrackingConfig config_;
    std::unique_ptr<IReidPipeline> reid_;
    int person_class_id_;
    KalmanFilter kalman_{KalmanFormat::XYWH};
    int frame_ = 0;
    int next_id_ = 0;
    CameraMotion motion_;
    float width_ = 0, height_ = 0;
    std::vector<Target> targets_;
    TrackingStep associate(const std::vector<Detection>& detections,
        const std::vector<std::vector<float>>& features);
};
