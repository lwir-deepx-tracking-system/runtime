#pragma once

#include "common/track.hpp"
#include "config/app_config.hpp"

struct GimbalDemand {
    int x = 0, y = 0;
    double speed = 0;
    double error_x = 0, error_y = 0;
    bool stop() const { return x == 0 && y == 0; }
};

// Pure calculation. Axis wiring signs belong exclusively to the device adapter.
class AutoTrackingController {
    AutoControlConfig config_;
public:
    explicit AutoTrackingController(AutoControlConfig config) : config_(config) {}
    bool valid(const Track&, int width, int height) const;
    GimbalDemand calculate(const Track&, int width, int height) const;
};
