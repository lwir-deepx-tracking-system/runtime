#include "control/auto_tracking_controller.hpp"

#include <algorithm>
#include <cmath>

bool AutoTrackingController::valid(const Track& t, int width, int height) const
{
    if (width <= 0 || height <= 0 || t.track_id <= 0 || t.predicted ||
        !std::isfinite(t.x) || !std::isfinite(t.y) ||
        !std::isfinite(t.width) || !std::isfinite(t.height) ||
        !std::isfinite(t.confidence) || t.width <= 0 || t.height <= 0 ||
        t.confidence < config_.min_confidence || t.confidence > 1)
        return false;
    const double cx = double(t.x) + double(t.width) / 2;
    const double cy = double(t.y) + double(t.height) / 2;
    return cx >= 0 && cx < width && cy >= 0 && cy < height;
}

GimbalDemand AutoTrackingController::calculate(const Track& t, int width, int height) const
{
    GimbalDemand d;
    if (!valid(t, width, height)) return d;
    d.error_x = (double(t.x) + double(t.width) / 2 - width / 2.0) / (width / 2.0);
    d.error_y = (double(t.y) + double(t.height) / 2 - height / 2.0) / (height / 2.0);
    if (config_.fov_x_deg > 0 && config_.fov_y_deg > 0) {
        constexpr double radians = 3.14159265358979323846 / 180;
        d.error_x = std::atan(d.error_x * std::tan(config_.fov_x_deg * radians / 2));
        d.error_y = std::atan(d.error_y * std::tan(config_.fov_y_deg * radians / 2));
    }
    d.x = std::abs(d.error_x) <= config_.deadband_x ? 0 : (d.error_x > 0 ? 1 : -1);
    // Image Y grows downward; the shared GUI/device command contract is +Y up.
    // Convert here so AUTO and manual commands use the same direction space.
    d.y = std::abs(d.error_y) <= config_.deadband_y ? 0 : (d.error_y > 0 ? -1 : 1);
    if (!d.stop())
        d.speed = std::clamp(config_.kp * std::max(d.x ? std::abs(d.error_x) : 0.0,
                                                 d.y ? std::abs(d.error_y) : 0.0),
                             double(config_.speed_min), double(config_.speed_max));
    return d;
}
