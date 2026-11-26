#include "tracking/tracker.hpp"

TrackingStep Tracker::process(const DetectionResult& input)
{
    TrackingStep result;
    const auto started = std::chrono::steady_clock::now();
    result.tracks = track(input.detections);
    result.association_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    result.raw_tracks = result.tracks;
    return result;
}

void Tracker::reset_for_model_change() { reset(); }

void Tracker::set_camera_motion(const CameraMotion&) {}

void Tracker::set_image_size(float, float) {}

void Tracker::set_confirmation_frames(int) {}

int Tracker::current_confirmation_frames() const { return 0; }
