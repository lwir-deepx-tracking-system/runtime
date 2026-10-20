#include "result_writer.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

bool timing_columns_align(const std::string& text)
{
    std::istringstream input(text);
    std::string separator;
    std::string header;
    std::string header_separator;
    std::string row_one;
    std::string row_two;
    if (!std::getline(input, separator) ||
        !std::getline(input, header) ||
        !std::getline(input, header_separator) ||
        !std::getline(input, row_one) ||
        !std::getline(input, row_two))
        return false;

    if (separator.size() != header.size() ||
        header_separator != separator ||
        header.size() != row_one.size() ||
        row_one.size() != row_two.size())
        return false;

    for (std::size_t position = 0; position < header.size(); ++position)
    {
        const bool header_bar = header[position] == '|';
        if (header_bar != (row_one[position] == '|') ||
            header_bar != (row_two[position] == '|'))
            return false;
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
    benchmark_result_writer::write_timing_row(
        timing, 488, 12.75, 20534.020, 13764.367, 103.5, 4.0);
    benchmark_result_writer::write_timing_footer(timing);

    const std::string timing_text = timing.str();
    if (timing_text.find("| frame_id | preprocess_ms") == std::string::npos ||
        timing_text.find("| total_latency_ms  |") == std::string::npos ||
        timing_text.find("0.125000") == std::string::npos ||
        timing_text.find("7.312184") == std::string::npos ||
        timing_text.find("20534.020000") == std::string::npos ||
        timing_text.find("|          0.031250 |          8.906250 |") == std::string::npos ||
        timing_text.find("|          4.000000 |      20654.270000 |") == std::string::npos ||
        !timing_columns_align(timing_text))
    {
        std::cerr << timing_text;
        return 1;
    }

    benchmark_result_writer::DxrtProfilerSample first;
    first.preprocess_us = 100.0;
    first.input_format_us = 8.0;
    first.h2d_us = 950.0;
    first.inference_core_all_us = 7200.0;
    first.d2h_us = 4.0;
    first.output_format_us = 18.0;
    first.npu_task_total_us = 8210.0;
    first.postprocess_us = 30.0;
    first.tracking_us = 10.0;
    first.host_latency_us = 8290.0;

    benchmark_result_writer::DxrtProfilerSample second = first;
    second.input_format_us = 12.0;
    second.h2d_us = 1050.0;
    second.inference_core_all_us = 7400.0;
    second.d2h_us = 8.0;
    second.output_format_us = 30.0;
    second.npu_task_total_us = 8510.0;
    second.preprocess_us = 200.0;
    second.postprocess_us = 50.0;
    second.tracking_us = 20.0;
    second.host_latency_us = 8690.0;

    std::ostringstream profiler;
    benchmark_result_writer::write_dxrt_profiler_summary(
        profiler, "seq_001", 2, {first, second});

    const std::string profiler_text = profiler.str();
    const std::string separator(78, '-');
    const auto preprocess_position = profiler_text.find("| Preprocess ");
    const auto npu_position = profiler_text.find("| NPU Input Format Handler ");
    const auto postprocess_position = profiler_text.find("| Postprocess ");
    const auto tracking_position = profiler_text.find("| Tracking ");
    const auto latency_position = profiler_text.find("| Host Latency ");
    const auto total_position = profiler_text.find("| total_latency ");
    if (profiler_text.find(
            "[Benchmark] DXRT PROFILER SUMMARY: seq_001 (2 frames)") ==
            std::string::npos ||
        preprocess_position == std::string::npos ||
        npu_position == std::string::npos ||
        postprocess_position == std::string::npos ||
        tracking_position == std::string::npos ||
        latency_position == std::string::npos ||
        total_position == std::string::npos ||
        !(preprocess_position < npu_position &&
          npu_position < latency_position &&
          latency_position < postprocess_position &&
          postprocess_position < tracking_position &&
          tracking_position < total_position) ||
        profiler_text.find(separator, preprocess_position) > npu_position ||
        profiler_text.find(separator, latency_position) > postprocess_position ||
        profiler_text.find(separator, tracking_position) > total_position ||
        profiler_text.find("Preprocess (Host)") != std::string::npos ||
        profiler_text.find("Postprocess (Host)") != std::string::npos ||
        profiler_text.find("Tracking (Host)") != std::string::npos ||
        profiler_text.find("     100.000 |      200.000 |      150.000") == std::string::npos ||
        profiler_text.find("       8.000") == std::string::npos ||
        profiler_text.find("      12.000") == std::string::npos ||
        profiler_text.find("      10.000") == std::string::npos ||
        profiler_text.find("      10.000 |       20.000 |       15.000") == std::string::npos ||
        profiler_text.find("    8430.000 |     8960.000 |     8695.000") == std::string::npos)
    {
        std::cerr << profiler_text;
        return 2;
    }

    return 0;
}
