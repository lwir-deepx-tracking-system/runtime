#pragma once

#include <optional>
#include <string>

// Only fields that can be changed without replacing Detection/ReID models.
struct TrackerPolicyUpdate
{
    std::optional<int> warmth_min;
    std::optional<int> warmth_max;
    std::optional<float> edge_birth;
    std::optional<int> confirmation_ms;
    std::optional<bool> gmc;
    std::optional<bool> class_gate;
    std::optional<std::string> position_memory;
    std::optional<std::string> box_refine_mode;
    std::optional<bool> box_refine_lost_same;
    std::optional<double> box_refine_aim_y;
};
