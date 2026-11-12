#pragma once

#include <algorithm>
#include <chrono>

// Display-only percentage estimate from successfully submitted commands.
// This is not measured joint position and must never feed control decisions.
class GimbalPositionEstimate {
public:
    using Clock = std::chrono::steady_clock;

    void advance(Clock::time_point now)
    {
        if (last_ != Clock::time_point{} && speed_ > 0) {
            const double elapsed = std::max(0.0, std::chrono::duration<double>(now - last_).count());
            const double units = elapsed * (std::max(5, speed_) / 5.0);
            pan_ = std::clamp(pan_ + x_ * units * (100.0 / 40), 0.0, 100.0);
            tilt_ = std::clamp(tilt_ + y_ * units * (100.0 / 14), 0.0, 100.0);
        }
        last_ = now;
    }

    void command(Clock::time_point now, int x, int y, int speed)
    {
        advance(now);
        x_ = x;
        y_ = y;
        speed_ = speed;
    }

    double pan() const { return pan_; }
    double tilt() const { return tilt_; }

private:
    Clock::time_point last_{};
    int x_ = 0, y_ = 0, speed_ = 0;
    double pan_ = 50.0, tilt_ = 50.0;
};
