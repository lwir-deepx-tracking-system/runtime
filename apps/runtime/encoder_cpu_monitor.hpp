#pragma once

#include <cstdint>
#include <optional>

#include "gui/gui_sender.hpp"

// Actual Runtime process accounting, not a replacement video pipeline.
// Times are supplied by the application so warmup/window arithmetic can be
// checked without manufacturing camera frames or inference results.
struct EncoderCpuSample
{
    double elapsed_seconds;
    double interval_seconds;
    double cpu_percent_one_core;
    double average_cpu_percent_one_core;
    double submitted_fps;
    double tx_fps;
    double average_tx_fps;
    std::uint64_t submitted;
    std::uint64_t encoded;
    bool final;
};

class EncoderCpuMonitor
{
    struct Snapshot
    {
        double wall = 0;
        double cpu = 0;
        GuiVideoStats video{};
    };
    enum class Phase { Waiting, Warmup, Measuring, Finished };
    Phase phase_ = Phase::Waiting;
    Snapshot warmup_, baseline_, previous_;

public:
    static constexpr double warmup_seconds = 10;
    static constexpr double measurement_seconds = 60;

    std::optional<EncoderCpuSample> poll(double wall, double cpu, const GuiVideoStats& video)
    {
        if (phase_ == Phase::Finished) return {};
        const Snapshot current{wall, cpu, video};
        if (phase_ == Phase::Waiting)
        {
            if (video.encoded_frames == 0) return {};
            warmup_ = current;
            phase_ = Phase::Warmup;
            return {};
        }
        if (phase_ == Phase::Warmup)
        {
            if (wall - warmup_.wall < warmup_seconds) return {};
            baseline_ = previous_ = current;
            phase_ = Phase::Measuring;
            return {};
        }

        // A restarted sender or a backwards clock must not produce unsigned
        // counter underflow or an average spanning two different windows.
        if (wall < previous_.wall || cpu < previous_.cpu ||
            video.submitted_frames < previous_.video.submitted_frames ||
            video.encoded_frames < previous_.video.encoded_frames)
        {
            phase_ = Phase::Waiting;
            return {};
        }
        const double interval = wall - previous_.wall;
        if (interval < 1) return {};
        const double elapsed = wall - baseline_.wall;
        EncoderCpuSample sample{
            elapsed, interval,
            100 * (cpu - previous_.cpu) / interval,
            100 * (cpu - baseline_.cpu) / elapsed,
            (video.submitted_frames - previous_.video.submitted_frames) / interval,
            (video.encoded_frames - previous_.video.encoded_frames) / interval,
            (video.encoded_frames - baseline_.video.encoded_frames) / elapsed,
            video.submitted_frames - baseline_.video.submitted_frames,
            video.encoded_frames - baseline_.video.encoded_frames,
            elapsed >= measurement_seconds
        };
        previous_ = current;
        if (sample.final) phase_ = Phase::Finished;
        return sample;
    }
};
