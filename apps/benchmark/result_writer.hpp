#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace benchmark_result_writer {

struct DxrtProfilerSample
{
    double input_format_us = 0.0;
    double h2d_us = 0.0;
    double inference_core_all_us = 0.0;
    double d2h_us = 0.0;
    double output_format_us = 0.0;
    double npu_task_total_us = 0.0;
    double host_latency_us = 0.0;
};

// CSV 형식을 유지하면서 timing 열의 시작 위치를 맞춰 기록한다.
void write_timing_header(std::ostream& file);
void write_timing_row(
    std::ostream& file,
    std::uint64_t frame_id,
    double preprocess_ms,
    double latency_ms,
    double npu_processing_ms,
    double postprocess_ms,
    double tracking_ms);

// Sequence의 DXRT profiler 통계를 터미널용 표로 기록한다.
void write_dxrt_profiler_summary(
    std::ostream& output,
    const std::string& sequence_name,
    std::size_t frame_count,
    const std::vector<DxrtProfilerSample>& samples);

}  // namespace benchmark_result_writer
