#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "common/detection.hpp"
#include "common/track.hpp"

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

// 사람이 읽기 위한 고정 폭 Detection 표의 헤더와 행을 기록한다.
void write_detection_summary_header(std::ostream& file);
void write_detection_summary_rows(
    std::ostream& file,
    std::uint64_t frame_id,
    const std::vector<Detection>& detections);

// 사람이 읽기 위한 고정 폭 Tracking 표의 헤더와 행을 기록한다.
void write_track_summary_header(std::ostream& file);
void write_track_summary_rows(
    std::ostream& file,
    std::uint64_t frame_id,
    const std::vector<Track>& tracks);

// 표의 마지막 구분선을 기록한다.
void write_summary_footer(std::ostream& file);

// Sequence의 DXRT profiler 통계를 터미널용 표로 기록한다.
void write_dxrt_profiler_summary(
    std::ostream& output,
    const std::string& sequence_name,
    std::size_t frame_count,
    const std::vector<DxrtProfilerSample>& samples);

}  // namespace benchmark_result_writer
