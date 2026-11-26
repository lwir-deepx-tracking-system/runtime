#include "inference/npu_core_reservation.hpp"

std::mutex NpuCoreReservation::mutex_;
unsigned NpuCoreReservation::occupied_ = 0;

NpuCoreReservation::NpuCoreReservation(NpuCoreBinding binding, const std::string& model) : mask_(mask(binding))
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (occupied_ & mask_)
        throw std::runtime_error("DXRT core reservation conflict for " + model +
            " (requested mask=" + std::to_string(mask_) +
            ", occupied mask=" + std::to_string(occupied_) + ")");
    occupied_ |= mask_;
}

NpuCoreReservation::~NpuCoreReservation()
{
    std::lock_guard<std::mutex> lock(mutex_);
    occupied_ &= ~mask_;
}

unsigned NpuCoreReservation::mask(NpuCoreBinding binding)
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
