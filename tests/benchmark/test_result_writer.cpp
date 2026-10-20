#include "result_writer.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

bool commas_align(const std::string& text)
{
    std::istringstream input(text);
    std::string header;
    std::string row;
    if (!std::getline(input, header) || !std::getline(input, row))
        return false;

    std::size_t header_position = 0;
    std::size_t row_position = 0;
    for (int column = 0; column < 5; ++column)
    {
        header_position = header.find(',', header_position);
        row_position = row.find(',', row_position);
        if (header_position == std::string::npos ||
            row_position != header_position)
        {
            return false;
        }
        ++header_position;
        ++row_position;
    }
    return true;
}

}  // namespace

int main()
{
    std::ostringstream timing;
    benchmark_result_writer::write_timing_header(timing);
    benchmark_result_writer::write_timing_row(
        timing, 1, 0.125, 8.5, 7.312184, 0.25, 0.03125);

    const std::string timing_text = timing.str();
    if (timing_text.find(
            "frame_id,preprocess_ms,latency_ms,npu_processing_ms,"
            "postprocess_ms,tracking_ms") == std::string::npos ||
        timing_text.find("0.125000") == std::string::npos ||
        timing_text.find("7.312184") == std::string::npos ||
        !commas_align(timing_text))
    {
        std::cerr << timing_text;
        return 1;
    }

    benchmark_result_writer::DxrtProfilerSample first;
    first.input_format_us = 8.0;
    first.h2d_us = 950.0;
    first.inference_core_all_us = 7200.0;
    first.d2h_us = 4.0;
    first.output_format_us = 18.0;
    first.npu_task_total_us = 8210.0;
    first.host_latency_us = 8290.0;

    benchmark_result_writer::DxrtProfilerSample second = first;
    second.input_format_us = 12.0;
    second.h2d_us = 1050.0;
    second.inference_core_all_us = 7400.0;
    second.d2h_us = 8.0;
    second.output_format_us = 30.0;
    second.npu_task_total_us = 8510.0;
    second.host_latency_us = 8690.0;

    std::ostringstream profiler;
    benchmark_result_writer::write_dxrt_profiler_summary(
        profiler, "seq_001", 2, {first, second});

    const std::string profiler_text = profiler.str();
    if (profiler_text.find(
            "[Benchmark] DXRT PROFILER SUMMARY: seq_001 (2 frames)") ==
            std::string::npos ||
        profiler_text.find("NPU Input Format Handler") == std::string::npos ||
        profiler_text.find("       8.000") == std::string::npos ||
        profiler_text.find("      12.000") == std::string::npos ||
        profiler_text.find("      10.000") == std::string::npos ||
        profiler_text.find("Host Latency") == std::string::npos)
    {
        std::cerr << profiler_text;
        return 2;
    }

    return 0;
}
