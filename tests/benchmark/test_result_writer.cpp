#include "result_writer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
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

bool profiler_columns_align(const std::string& text)
{
    std::istringstream input(text);
    std::string line;
    std::getline(input, line);  // sequence title
    while (std::getline(input, line))
    {
        if (line.empty() || line.rfind("[ ", 0) == 0)
            continue;
        if (line.size() != 80)
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
    first.npu_devices.push_back({"npu_0", 0, 8.0, 950.0, 7200.0,
                                 7200.0, 0.0, 0.0, 4.0, 18.0, 8210.0});
    second.npu_devices.push_back({"npu_0", 0, 12.0, 1050.0, 7400.0,
                                  7400.0, 0.0, 0.0, 8.0, 30.0, 8510.0});
    second.npu_devices.push_back({"npu_1", 2, 2.0, 100.0, 3000.0,
                                  0.0, 3000.0, 0.0, 5.0, 7.0, 3114.0});
    first.cpu_task_us = 1000.0;
    second.cpu_task_us = 3000.0;

    std::ostringstream profiler;
    benchmark_result_writer::write_dxrt_profiler_summary(
        profiler, "seq_001", 2, {first, second});

    const std::string profiler_text = profiler.str();
    const std::string separator(80, '-');
    const std::string double_separator(80, '=');
    const auto preprocess_position = profiler_text.find("[ PREPROCESS ]");
    const auto npu_position = profiler_text.find("[ NPU TASK - npu_0 / Device 0 ]");
    const auto second_npu_position = profiler_text.find("[ NPU TASK - npu_1 / Device 2 ]");
    const auto cpu_position = profiler_text.find("[ CPU TASK - cpu_0 / ONNX Runtime ]");
    const auto postprocess_position = profiler_text.find("[ C++ POSTPROCESS ]");
    const auto tracking_position = profiler_text.find("[ TRACKING ]");
    const auto latency_position = profiler_text.find("[ LATENCY SUMMARY ]");
    const auto total_position = profiler_text.find("TOTAL LATENCY");
    if (profiler_text.find(
            "[Benchmark] DXRT PROFILER SUMMARY: seq_001 (2 frames)") ==
            std::string::npos ||
        preprocess_position == std::string::npos ||
        npu_position == std::string::npos ||
        second_npu_position == std::string::npos ||
        cpu_position == std::string::npos ||
        postprocess_position == std::string::npos ||
        tracking_position == std::string::npos ||
        latency_position == std::string::npos ||
        total_position == std::string::npos ||
        !(preprocess_position < npu_position &&
          npu_position < second_npu_position &&
          second_npu_position < cpu_position &&
          cpu_position < postprocess_position &&
          postprocess_position < tracking_position &&
          tracking_position < latency_position &&
          latency_position < total_position) ||
        profiler_text.find(separator, preprocess_position) > npu_position ||
        profiler_text.find(separator, npu_position) > second_npu_position ||
        profiler_text.find(double_separator, latency_position) > total_position ||
        profiler_text.find("Preprocess                             100.0       200.0       150.0        47.1") == std::string::npos ||
        profiler_text.find("  - Inference Core 1                       -           -           -           -") == std::string::npos ||
        profiler_text.find("CPU Task (total)                      1000.0      3000.0      2000.0        70.7") == std::string::npos ||
        profiler_text.find("TOTAL LATENCY                         8430.0      8960.0      8695.0         4.3") == std::string::npos ||
        !profiler_columns_align(profiler_text))
    {
        std::cerr << profiler_text;
        return 2;
    }

    first.cpu_task_us.reset();
    std::ostringstream no_cpu_profiler;
    benchmark_result_writer::write_dxrt_profiler_summary(
        no_cpu_profiler, "seq_002", 1, {first});
    const std::string no_cpu_text = no_cpu_profiler.str();
    const auto no_cpu_position = no_cpu_text.find("CPU Task (total)");
    if (no_cpu_position == std::string::npos ||
        std::count(no_cpu_text.begin() + no_cpu_position,
                   no_cpu_text.begin() + no_cpu_text.find('\n', no_cpu_position),
                   '-') != 4)
        return 3;

    second.npu_devices.front().inference_core_1_us = 1200.0;
    std::ostringstream active_core_profiler;
    benchmark_result_writer::write_dxrt_profiler_summary(
        active_core_profiler, "seq_003", 2, {first, second});
    const std::string active_core_text = active_core_profiler.str();
    const auto core_position = active_core_text.find("  - Inference Core 1");
    if (core_position == std::string::npos)
        return 4;
    const std::string core_row = active_core_text.substr(
        core_position, active_core_text.find('\n', core_position) - core_position);
    if (core_row.find("1200.0      1200.0      1200.0         0.0") ==
            std::string::npos)
        return 4;

    const auto file_path = std::filesystem::temp_directory_path() /
        ("lwir-profiler-summary-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
    std::ostringstream terminal;
    benchmark_result_writer::write_dxrt_profiler_summary_outputs(
        terminal, file_path, "seq_001", 2, {first, second});
    std::ifstream saved(file_path, std::ios::binary);
    const std::string saved_text(
        (std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    std::filesystem::remove(file_path);
    if (saved_text != terminal.str() || saved_text.empty())
        return 5;

    try
    {
        benchmark_result_writer::write_dxrt_profiler_summary_outputs(
            terminal, file_path / "missing" / "summary.txt",
            "seq_001", 2, {first, second});
        return 6;
    }
    catch (const std::runtime_error& error)
    {
        if (std::string(error.what()).find("summary.txt") == std::string::npos)
            return 7;
    }

    return 0;
}
