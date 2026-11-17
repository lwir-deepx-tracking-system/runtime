#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include "common/frame.hpp"
#include "common/track.hpp"

struct RawRecordingStatus {
    bool active = false;
    bool failed = false;
    std::uint64_t frames = 0;
    std::uint64_t dropped = 0;
    std::uint32_t session = 0;
};

// Records captured CV_16UC1 frames before the detection queue can discard them.
class RawRecorder final {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit RawRecorder(std::filesystem::path root);
    ~RawRecorder();
    RawRecorder(const RawRecorder&) = delete;
    RawRecorder& operator=(const RawRecorder&) = delete;
    bool start(const std::string& name);
    bool stop();
    void submit(FrameContextPtr frame) noexcept;
    void submit_tracks(TrackingResultPtr result, double tracker_ms = -1.0) noexcept;
    RawRecordingStatus status() const;
    std::filesystem::path directory() const;
};
