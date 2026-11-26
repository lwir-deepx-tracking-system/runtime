#include "app/camera_watchdog.hpp"

#ifdef LWIR_ENABLE_SYSTEMD_WATCHDOG
#include <cstdint>
#include <systemd/sd-daemon.h>
#endif

void CameraWatchdog::ready() noexcept
{
#ifdef LWIR_ENABLE_SYSTEMD_WATCHDOG
    sd_notify(0, "READY=1");
#endif
}

void CameraWatchdog::progress() noexcept
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
