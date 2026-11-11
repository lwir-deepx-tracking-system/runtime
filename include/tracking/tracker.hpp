#pragma once

#include <vector>

#include "common/detection.hpp"
#include "common/track.hpp"
#include "tracking/camera_motion.hpp"
#include <cstdint>
#include <chrono>
#include <string>

struct TrackingEmbedding
{
    std::size_t source_index = 0; // detection index before tracking, track ID after tracking
    int track_id = -1;
    cv::Rect crop;
    std::vector<float> values;
    double elapsed_ms = 0;
};

struct TrackingStep
{
    std::vector<Track> tracks;
    std::vector<Track> raw_tracks;
    std::vector<TrackingEmbedding> embeddings;
    std::vector<std::string> association_events;
    double reid_ms = 0;
    double association_ms = 0;
    double matching_ms = 0; // post-tracking stable-ID matcher; zero for BoT-SORT
};


// Tracking 알고리즘이 따라야 하는 공통 인터페이스
class Tracker
{
public:
    virtual ~Tracker() = default;

    // 현재 Frame의 Detection 목록을 받아 Track 목록을 생성한다.
    virtual std::vector<Track> track(
        const std::vector<Detection>& detections
    ) = 0;

    // Frame-aware path used by both applications. Legacy direct callers use track().
    virtual TrackingStep process(const DetectionResult& input)
    {
        TrackingStep result;
        const auto started = std::chrono::steady_clock::now();
        result.tracks = track(input.detections);
        result.association_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        result.raw_tracks = result.tracks;
        return result;
    }

    // Tracker가 유지하는 내부 상태를 초기화한다.
    virtual void reset() = 0;
    virtual void set_camera_motion(const CameraMotion&) {}
    virtual void set_image_size(float, float) {}
};
