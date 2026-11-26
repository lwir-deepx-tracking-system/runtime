#include "common/detection_timing.hpp"

double DetectionTiming::preprocess_ms() const
{
    return elapsed_ms(preprocess_started_at, preprocess_finished_at);
}

double DetectionTiming::latency_ms() const
{
    return elapsed_ms(inference_started_at, inference_finished_at);
}

double DetectionTiming::npu_processing_ms() const
{
    return npu_processing_duration_ms;
}

double DetectionTiming::postprocess_ms() const
{
    return elapsed_ms(postprocess_started_at, postprocess_finished_at);
}

double DetectionTiming::elapsed_ms(TimePoint started_at, TimePoint finished_at)
{
    return std::chrono::duration<double, std::milli>(
        finished_at - started_at).count();
}
