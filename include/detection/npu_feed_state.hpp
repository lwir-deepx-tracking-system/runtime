#pragma once

#include <cstdint>

// Live frame-routing state exposed without transferring ownership of a session.
struct NpuFeedState {
    bool available = false;
    std::uint8_t requested = 0; // 0 normal, 1 alternate, 2 auto
    bool alternating = false;
    std::uint32_t core0 = 0;
    std::uint32_t core1 = 0;
};
