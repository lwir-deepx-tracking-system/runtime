#include "control/gimbal_position_estimate.hpp"

#include <algorithm>

void GimbalPositionEstimate::advance(Clock::time_point now)
{
    if (last_ != Clock::time_point{} && speed_ > 0) {
        const double elapsed = std::max(0.0, std::chrono::duration<double>(now - last_).count());
        const double units = elapsed * (std::max(5, speed_) / 5.0);
        pan_ = std::clamp(pan_ + x_ * units * (100.0 / 40), 0.0, 100.0);
        tilt_ = std::clamp(tilt_ + y_ * units * (100.0 / 14), 0.0, 100.0);
    }
    last_ = now;
}

void GimbalPositionEstimate::command(Clock::time_point now, int x, int y, int speed)
{
    advance(now);
    x_ = x;
    y_ = y;
    speed_ = speed;
}

double GimbalPositionEstimate::pan() const { return pan_; }

double GimbalPositionEstimate::tilt() const { return tilt_; }
