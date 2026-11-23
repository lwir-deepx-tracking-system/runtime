#pragma once

#include <chrono>
#include <cstdint>
#ifdef LWIR_ENABLE_SYSTEMD_WATCHDOG
#include <systemd/sd-daemon.h>
#endif

// Only capture progress feeds the supervisor. A stuck vendor call stops updates.
class CameraWatchdog {
public:
    static void ready() noexcept
    {
#ifdef LWIR_ENABLE_SYSTEMD_WATCHDOG
        sd_notify(0, "READY=1");
#endif
    }

    void progress() noexcept
    {
#ifdef LWIR_ENABLE_SYSTEMD_WATCHDOG
        if (!initialized_) {
            std::uint64_t usec = 0;
            if (sd_watchdog_enabled(0, &usec) > 0)
                interval_ = std::chrono::microseconds(usec / 3);
            initialized_ = true;
        }
        if (interval_ == std::chrono::microseconds::zero()) return;
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_) {
            sd_notify(0, "WATCHDOG=1");
            next_ = now + interval_;
        }
#endif
    }

private:
#ifdef LWIR_ENABLE_SYSTEMD_WATCHDOG
    bool initialized_ = false;
    std::chrono::microseconds interval_{0};
    std::chrono::steady_clock::time_point next_{};
#endif
};
