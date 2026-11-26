#pragma once

#include <chrono>

// Display-only percentage estimate from successfully submitted commands.
// This is not measured joint position and must never feed control decisions.
class GimbalPositionEstimate {
public:
    using Clock = std::chrono::steady_clock;

    void advance(Clock::time_point now);

    void command(Clock::time_point now, int x, int y, int speed);

    double pan() const;
    double tilt() const;

private:
    Clock::time_point last_{};
    int x_ = 0, y_ = 0, speed_ = 0;
    double pan_ = 50.0, tilt_ = 50.0;
};
