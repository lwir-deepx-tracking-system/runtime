#pragma once

#include <memory>
#include <atomic>
#include <chrono>
#include <deque>
#include <opencv2/core.hpp>
#include "config/app_config.hpp"
#include "tracking/tracker.hpp"

// Per-tracker state; reset/epoch replacement also clears the GMC history.
class FramePolicyTracker final : public Tracker {
public:
    FramePolicyTracker(std::unique_ptr<Tracker> base, TrackingFrameConfig config,
                       int base_confirmation_frames = 2);
    std::vector<Track> track(const std::vector<Detection>& detections) override;
    TrackingStep process(const DetectionResult& input) override;
    void reset() override;
    int current_confirmation_frames() const override { return effective_confirmation_frames_.load(); }
private:
    CameraMotion estimate(const cv::Mat& raw);
    void update_confirmation(std::chrono::steady_clock::time_point captured_at);
    std::unique_ptr<Tracker> base_;
    TrackingFrameConfig config_;
    cv::Mat previous_;
    std::vector<cv::Point2f> points_;
    int base_confirmation_frames_ = 2;
    int time_confirmation_frames_ = 0;
    std::atomic<int> effective_confirmation_frames_{0};
    std::chrono::steady_clock::time_point last_confirmation_frame_{};
    std::deque<double> confirmation_intervals_;
};

// NumPy percentile(..., 95) with linear interpolation, sampled at stride 2.
double warmth_percentile(const cv::Mat& raw, const cv::Rect& roi);
