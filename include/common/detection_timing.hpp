#pragma once

#include <chrono>
#include <cstdint>

// 비동기 요청 하나에 귀속되는 Detection 내부 단계별 측정 시각.
// steady_clock을 사용해 시스템 시각 변경의 영향을 받지 않는다.
struct DetectionTiming
{
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::uint64_t frame_id = 0;
    TimePoint preprocess_started_at{};
    TimePoint preprocess_finished_at{};
    TimePoint inference_started_at{};
    TimePoint inference_finished_at{};
    TimePoint postprocess_started_at{};
    TimePoint postprocess_finished_at{};

    double preprocess_ms() const
    {
        return elapsed_ms(preprocess_started_at, preprocess_finished_at);
    }

    double inference_ms() const
    {
        return elapsed_ms(inference_started_at, inference_finished_at);
    }

    double postprocess_ms() const
    {
        return elapsed_ms(postprocess_started_at, postprocess_finished_at);
    }

private:
    static double elapsed_ms(TimePoint started_at, TimePoint finished_at)
    {
        return std::chrono::duration<double, std::milli>(
            finished_at - started_at).count();
    }
};
