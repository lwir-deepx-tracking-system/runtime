#pragma once

#include "config/app_config.hpp"

#include <mutex>
#include <stdexcept>
#include <string>

// Process-local DXRT reservation. Current DXRT options select all visible devices.
class NpuCoreReservation {
public:
    NpuCoreReservation(NpuCoreBinding binding, const std::string& model) : mask_(mask(binding))
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (occupied_ & mask_)
            throw std::runtime_error("DXRT core reservation conflict for " + model +
                " (requested mask=" + std::to_string(mask_) +
                ", occupied mask=" + std::to_string(occupied_) + ")");
        occupied_ |= mask_;
    }

    ~NpuCoreReservation()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        occupied_ &= ~mask_;
    }

    NpuCoreReservation(const NpuCoreReservation&) = delete;
    NpuCoreReservation& operator=(const NpuCoreReservation&) = delete;

private:
    static unsigned mask(NpuCoreBinding binding)
    {
        switch (binding) {
        case NpuCoreBinding::All: return 7;
        case NpuCoreBinding::Core0: return 1;
        case NpuCoreBinding::Core1: return 2;
        case NpuCoreBinding::Core2: return 4;
        case NpuCoreBinding::Core01: return 3;
        case NpuCoreBinding::Core12: return 6;
        case NpuCoreBinding::Core02: return 5;
        }
        throw std::invalid_argument("Invalid DXRT core binding");
    }

    inline static std::mutex mutex_;
    inline static unsigned occupied_ = 0;
    unsigned mask_;
};
