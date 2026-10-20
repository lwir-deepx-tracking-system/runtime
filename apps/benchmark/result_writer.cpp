#include "result_writer.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <ostream>
#include <string>

namespace benchmark_result_writer {
namespace {

constexpr int kFrameIdWidth = 8;
constexpr int kPreprocessWidth = 13;
constexpr int kLatencyWidth = 10;
constexpr int kNpuProcessingWidth = 17;
constexpr int kPostprocessWidth = 14;
constexpr int kTrackingWidth = 11;
constexpr int kProfilerNameWidth = 29;
constexpr int kProfilerValueWidth = 12;

struct Statistics
{
    double minimum = 0.0;
    double maximum = 0.0;
    double average = 0.0;
};

template <typename Selector>
Statistics calculate_statistics(
    const std::vector<DxrtProfilerSample>& samples,
    Selector selector)
{
    Statistics result;
    result.minimum = std::numeric_limits<double>::max();
    result.maximum = std::numeric_limits<double>::lowest();

    for (const DxrtProfilerSample& sample : samples)
    {
        const double value = selector(sample);
        result.minimum = std::min(result.minimum, value);
        result.maximum = std::max(result.maximum, value);
        result.average += value;
    }
    result.average /= static_cast<double>(samples.size());
    return result;
}

void write_profiler_row(
    std::ostream& output,
    const char* name,
    const Statistics& statistics)
{
    output << "| " << std::left << std::setw(kProfilerNameWidth) << name
           << " | " << std::right << std::fixed << std::setprecision(3)
           << std::setw(kProfilerValueWidth) << statistics.minimum
           << " | " << std::setw(kProfilerValueWidth) << statistics.maximum
           << " | " << std::setw(kProfilerValueWidth) << statistics.average
           << " |\n";
}

}  // namespace

void write_timing_header(std::ostream& file)
{
    file << "frame_id,preprocess_ms,latency_ms,npu_processing_ms,"
            "postprocess_ms,tracking_ms\n";
}

void write_timing_row(
    std::ostream& file,
    std::uint64_t frame_id,
    double preprocess_ms,
    double latency_ms,
    double npu_processing_ms,
    double postprocess_ms,
    double tracking_ms)
{
    file << std::fixed << std::setprecision(6) << std::right
         << std::setw(kFrameIdWidth) << frame_id << ','
         << std::setw(kPreprocessWidth) << preprocess_ms << ','
         << std::setw(kLatencyWidth) << latency_ms << ','
         << std::setw(kNpuProcessingWidth) << npu_processing_ms << ','
         << std::setw(kPostprocessWidth) << postprocess_ms << ','
         << std::setw(kTrackingWidth) << tracking_ms << '\n';
}

void write_dxrt_profiler_summary(
    std::ostream& output,
    const std::string& sequence_name,
    std::size_t frame_count,
    const std::vector<DxrtProfilerSample>& samples)
{
    if (samples.empty())
        return;

    const int table_width =
        kProfilerNameWidth + 3 * kProfilerValueWidth + 13;
    const std::string separator(table_width, '-');

    output << "[Benchmark] DXRT PROFILER SUMMARY: " << sequence_name
           << " (" << frame_count << " frames)\n"
           << separator << '\n'
           << "| " << std::left << std::setw(kProfilerNameWidth) << "Name"
           << " | " << std::right << std::setw(kProfilerValueWidth) << "min (us)"
           << " | " << std::setw(kProfilerValueWidth) << "max (us)"
           << " | " << std::setw(kProfilerValueWidth) << "average (us)"
           << " |\n"
           << separator << '\n';

    write_profiler_row(output, "NPU Input Format Handler", calculate_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.input_format_us; }));
    write_profiler_row(output, "H2D (PCIe Write)", calculate_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.h2d_us; }));
    write_profiler_row(output, "NPU Inference (Core All)", calculate_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.inference_core_all_us; }));
    write_profiler_row(output, "D2H (PCIe Read)", calculate_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.d2h_us; }));
    write_profiler_row(output, "NPU Output Format Handler", calculate_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.output_format_us; }));
    write_profiler_row(output, "NPU Task Total", calculate_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.npu_task_total_us; }));
    write_profiler_row(output, "Host Latency", calculate_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.host_latency_us; }));

    output << separator << '\n';
}

}  // namespace benchmark_result_writer
