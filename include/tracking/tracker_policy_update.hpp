#pragma once

#include <optional>

// Only fields that can be changed without replacing Detection/ReID models.
struct TrackerPolicyUpdate
{
    std::optional<int> warmth_min;
    std::optional<int> warmth_max;
    std::optional<float> edge_birth;
    std::optional<int> confirmation_ms;
    std::optional<bool> gmc;
    std::optional<bool> class_gate;
};
