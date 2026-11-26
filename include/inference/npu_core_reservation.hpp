#pragma once

#include "config/app_config.hpp"

#include <mutex>
#include <stdexcept>
#include <string>

// Process-local DXRT reservation. Current DXRT options select all visible devices.
class NpuCoreReservation {
public:
    NpuCoreReservation(NpuCoreBinding binding, const std::string& model);

    ~NpuCoreReservation();

    NpuCoreReservation(const NpuCoreReservation&) = delete;
    NpuCoreReservation& operator=(const NpuCoreReservation&) = delete;

private:
    static unsigned mask(NpuCoreBinding binding);

    static std::mutex mutex_;
    static unsigned occupied_;
    unsigned mask_;
};
