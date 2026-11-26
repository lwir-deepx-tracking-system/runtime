#pragma once

#include "control/gimbal_device.hpp"
#include <string_view>

// Waveshare General_Driver baseInfoFeedback: flat numeric JSON, T=1001,
// pan and tilt in degrees. Ignore command echoes and other packet types.
std::optional<GimbalPose> parse_waveshare_pose(std::string_view line);
