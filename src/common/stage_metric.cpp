#include "common/stage_metric.hpp"

StageMetric make_stage_metric(
    const FrameMetadata& metadata,
    std::chrono::steady_clock::time_point enqueued_at,
    std::chrono::steady_clock::time_point queue_started_at,
    std::chrono::steady_clock::time_point processing_started_at,
    std::chrono::steady_clock::time_point finished_at,
    bool include_e2e)
{
    StageMetric metric;
    metric.frame_id = metadata.frame_id;
    metric.processing_ms =
        std::chrono::duration<double, std::milli>(
            finished_at - processing_started_at).count();
    if (enqueued_at != std::chrono::steady_clock::time_point{})
        metric.queue_wait_ms =
            std::chrono::duration<double, std::milli>(
                queue_started_at - enqueued_at).count();
    if (include_e2e && metadata.captured_at != std::chrono::steady_clock::time_point{})
        metric.e2e_ms =
            std::chrono::duration<double, std::milli>(finished_at - metadata.captured_at).count();
    return metric;
}

void record_stage_metric(
    std::vector<StageMetric>& metrics, const FrameMetadata& metadata,
    std::chrono::steady_clock::time_point enqueued_at,
    std::chrono::steady_clock::time_point queue_started_at,
    std::chrono::steady_clock::time_point processing_started_at,
    std::chrono::steady_clock::time_point finished_at, bool include_e2e)
{
    if (metrics.size() < kMaxStageMetrics)
        metrics.push_back(make_stage_metric(metadata, enqueued_at, queue_started_at,
            processing_started_at, finished_at, include_e2e));
}
