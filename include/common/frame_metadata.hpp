#pragma once

#include <chrono>
#include <cstdint>

// 같은 프레임의 단계별 결과를 연결하는 공통 정보.
struct FrameMetadata
{
    std::uint64_t frame_id = 0;
    std::chrono::steady_clock::time_point captured_at{};
};
