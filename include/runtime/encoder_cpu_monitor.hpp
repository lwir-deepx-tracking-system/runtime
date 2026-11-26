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

    std::optional<EncoderCpuSample> poll(double wall, double cpu, const GuiVideoStats& video);
};
