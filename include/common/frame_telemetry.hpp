#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Shared with PC src/metadata/RuntimeTelemetry.h. Values are float32 milliseconds.
constexpr std::uint32_t kTelemetryMagic = 0x4c57544dU; // LWTM
constexpr std::size_t kTelemetrySize = 124;
constexpr std::size_t kTelemetryMetricCount = 18;
constexpr std::uint32_t kTelemetryValidBits = (1U << kTelemetryMetricCount) - 1;

struct FrameTelemetrySnapshot {
    std::uint64_t session_id = 0, model_generation = 0, tracker_epoch = 0, frame_id = 0;
    std::uint32_t valid_mask = 0;
    float sample_age_ms = 0;
    std::uint32_t model_id = 0, tracker_id = 0;
    std::array<float, kTelemetryMetricCount> values{};
};
