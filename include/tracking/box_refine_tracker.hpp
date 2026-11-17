#pragma once

#include <chrono>
#include <memory>
#include <unordered_map>

#include "config/app_config.hpp"
#include "tracking/tracker.hpp"

// Refines only the published Track boxes. Association and raw_tracks stay unchanged.
class BoxRefineTracker final : public Tracker {
public:
    BoxRefineTracker(std::unique_ptr<Tracker> base, BoxRefineConfig config);
    std::vector<Track> track(const std::vector<Detection>& detections) override;
    TrackingStep process(const DetectionResult& input) override;
    void reset() override;
    void reset_for_model_change() override;
    void set_camera_motion(const CameraMotion& motion) override;
    void set_image_size(float width, float height) override { base_->set_image_size(width,height); }
    void set_confirmation_frames(int frames) override { base_->set_confirmation_frames(frames); }
    int current_confirmation_frames() const override { return base_->current_confirmation_frames(); }
private:
    struct SizeState {
        double log_w = 0, log_h = 0, dw = 0, dh = 0;
        std::chrono::steady_clock::time_point time{};
        std::uint64_t last_frame = 0;
        Track last_input{};
        Track last_output{};
        bool has_last = false;
        bool pose_family = false;
        std::chrono::steady_clock::time_point blend_start{};
        double blend_x = 0, blend_y = 0;
    };
    void refine(std::vector<Track>& tracks, std::chrono::steady_clock::time_point time,
                std::uint64_t frame_id, bool pose_family);
    std::unique_ptr<Tracker> base_;
    BoxRefineConfig config_;
    std::unordered_map<int,SizeState> sizes_;
    std::uint64_t next_frame_ = 0;
    bool pose_family_ = false;
    double camera_log_scale_ = 0;
    bool switch_pending_ = false;
};
