#pragma once

#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include "config/app_config.hpp"

struct GimbalPose { double pan = 0, tilt = 0; };

// Device strategy: application commands never contain vendor JSON or serial FDs.
class IGimbalDevice {
public:
    virtual ~IGimbalDevice() = default;
    virtual void move(int x_direction, int y_direction) = 0;
    // Implementations without variable speed must reject AUTO, never silently ignore SPD.
    virtual void move_with_speed(int, int, int);
    virtual void stop() = 0;
    virtual void start_pose_feedback();
    virtual std::optional<GimbalPose> read_pose();
    virtual void home() = 0; // Request only; does not establish physical completion.
};

// Shared encoder also permits testing vendor commands without a physical device.
class WaveshareCommands {
public:
    static std::string move(const GimbalConfig&, int x_direction, int y_direction);
    static std::string move(const GimbalConfig&, int x_direction, int y_direction, int speed);
    static std::string stop();
    static std::string home(const GimbalConfig&);
};

class GimbalDeviceFactory {
public:
    static std::unique_ptr<IGimbalDevice> create(const GimbalConfig&);
};
