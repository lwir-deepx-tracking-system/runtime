#pragma once

#include <memory>
#include <opencv2/core.hpp>
#include "config/app_config.hpp"
#include "tracking/tracker.hpp"

// Per-tracker state; reset/epoch replacement also clears the GMC history.
class FramePolicyTracker final : public Tracker {
public:
    FramePolicyTracker(std::unique_ptr<Tracker> base, TrackingFrameConfig config);
    std::vector<Track> track(const std::vector<Detection>& detections) override;
    TrackingStep process(const DetectionResult& input) override;
    void reset() override;
private:
    CameraMotion estimate(const cv::Mat& raw);
    std::unique_ptr<Tracker> base_;
    TrackingFrameConfig config_;
    cv::Mat previous_;
    std::vector<cv::Point2f> points_;
};

// NumPy percentile(..., 95) with linear interpolation, sampled at stride 2.
double warmth_percentile(const cv::Mat& raw, const cv::Rect& roi);
