#pragma once

#include <chrono>

// Only capture progress feeds the supervisor. A stuck vendor call stops updates.
class CameraWatchdog {
public:
    static void ready() noexcept;

    void progress() noexcept;

private:
#ifdef LWIR_ENABLE_SYSTEMD_WATCHDOG
    bool initialized_ = false;
    std::chrono::microseconds interval_{0};
    std::chrono::steady_clock::time_point next_{};
#endif
};
