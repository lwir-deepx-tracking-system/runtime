#include "result_writer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace benchmark_result_writer {
namespace {

constexpr int kFrameIdWidth = 8;
constexpr int kTimingValueWidth = 17;
constexpr int kTimingColumnCount = 7;
constexpr int kTimingTableWidth =
    kFrameIdWidth + (kTimingColumnCount - 1) * kTimingValueWidth +
    3 * kTimingColumnCount + 1;
constexpr int kProfilerNameWidth = 32;
constexpr int kProfilerValueWidth = 12;
constexpr int kProfilerTableWidth = kProfilerNameWidth + 4 * kProfilerValueWidth;

struct Statistics
{
    bool valid = false;
    double minimum = 0.0;
    double maximum = 0.0;
    double average = 0.0;
    double cov = 0.0;
};

Statistics calculate_statistics(const std::vector<double>& values)
{
    Statistics result;
    if (values.empty())
        return result;
    result.valid = true;
    result.minimum = std::numeric_limits<double>::max();
    result.maximum = std::numeric_limits<double>::lowest();

    for (double value : values)
    {
        result.minimum = std::min(result.minimum, value);
        result.maximum = std::max(result.maximum, value);
        result.average += value;
    }
    result.average /= static_cast<double>(values.size());
    if (values.size() > 1 && result.average != 0.0)
    {
        double sum_squares = 0.0;
        for (double value : values)
            sum_squares += (value - result.average) * (value - result.average);
        result.cov = std::sqrt(sum_squares / (values.size() - 1)) /
            result.average * 100.0;
    }
    return result;
}

template <typename Selector>
Statistics sample_statistics(
    const std::vector<DxrtProfilerSample>& samples, Selector selector)
{
    std::vector<double> values;
    values.reserve(samples.size());
    for (const auto& sample : samples)
    {
        const std::optional<double> value = selector(sample);
        if (value)
            values.push_back(*value);
    }
    return calculate_statistics(values);
}

void write_profiler_value(std::ostream& output, double value, bool valid)
{
    output << std::right << std::setw(kProfilerValueWidth);
    if (valid)
        output << std::fixed << std::setprecision(1) << value;
    else
        output << '-';
}

void write_profiler_row(
    std::ostream& output,
    const char* name,
    const Statistics& statistics)
{
    output << std::left << std::setw(kProfilerNameWidth) << name;
    write_profiler_value(output, statistics.minimum, statistics.valid);
    write_profiler_value(output, statistics.maximum, statistics.valid);
    write_profiler_value(output, statistics.average, statistics.valid);
    write_profiler_value(output, statistics.cov, statistics.valid);
    output << '\n';
}

double total_latency_us(const DxrtProfilerSample& sample)
{
    return sample.preprocess_us + sample.host_latency_us +
        sample.postprocess_us + sample.tracking_us;
}

}  // namespace

void write_timing_header(std::ostream& file)
{
    const std::string separator(kTimingTableWidth, '-');
    file << separator << '\n'
         << "| " << std::left << std::setw(kFrameIdWidth) << "frame_id"
         << " | " << std::setw(kTimingValueWidth) << "preprocess_ms"
         << " | " << std::setw(kTimingValueWidth) << "latency_ms"
         << " | " << std::setw(kTimingValueWidth) << "npu_processing_ms"
         << " | " << std::setw(kTimingValueWidth) << "postprocess_ms"
         << " | " << std::setw(kTimingValueWidth) << "tracking_ms"
         << " | " << std::setw(kTimingValueWidth) << "total_latency_ms"
         << " |\n" << separator << '\n';
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
         << "| " << std::setw(kFrameIdWidth) << frame_id
         << " | " << std::setw(kTimingValueWidth) << preprocess_ms
         << " | " << std::setw(kTimingValueWidth) << latency_ms
         << " | " << std::setw(kTimingValueWidth) << npu_processing_ms
         << " | " << std::setw(kTimingValueWidth) << postprocess_ms
         << " | " << std::setw(kTimingValueWidth) << tracking_ms
         << " | " << std::setw(kTimingValueWidth)
         << (preprocess_ms + latency_ms + postprocess_ms + tracking_ms)
         << " |\n";
}

void write_timing_footer(std::ostream& file)
{
    file << std::string(kTimingTableWidth, '-') << '\n';
}

void write_dxrt_profiler_summary(
    std::ostream& output,
    const std::string& sequence_name,
    std::size_t frame_count,
    const std::vector<DxrtProfilerSample>& samples)
{
    if (samples.empty())
        return;

    const std::string separator(kProfilerTableWidth, '=');
    const std::string section_separator(kProfilerTableWidth, '-');

    output << "[Benchmark] DXRT PROFILER SUMMARY: " << sequence_name
           << " (" << frame_count << " frames)\n\n"
           << separator << '\n'
           << std::left << std::setw(kProfilerNameWidth) << "Name"
           << std::right << std::setw(kProfilerValueWidth) << "min(us)"
           << std::setw(kProfilerValueWidth) << "max(us)"
           << std::setw(kProfilerValueWidth) << "avg(us)"
           << std::setw(kProfilerValueWidth) << "CoV(%)"
           << '\n'
           << separator << '\n';

    output << "[ PREPROCESS ]\n";
    write_profiler_row(output, "Preprocess", sample_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.preprocess_us; }));
    output << section_separator << '\n';

    std::set<std::pair<std::string, int>> devices;
    for (const auto& sample : samples)
        for (const auto& device : sample.npu_devices)
            devices.emplace(device.task_name, device.device_id);

    for (const auto& key : devices)
    {
        output << "[ NPU TASK - " << key.first << " / Device "
               << key.second << " ]\n";
        const auto device_statistics = [&](double DxrtProfilerSample::NpuDeviceSample::* field,
                                           bool positive_only = false) {
            return sample_statistics(samples, [&](const DxrtProfilerSample& sample)
                -> std::optional<double> {
                for (const auto& device : sample.npu_devices)
                    if (device.task_name == key.first && device.device_id == key.second)
                    {
                        const double value = device.*field;
                        if (positive_only && value <= 0.0)
                            return std::nullopt;
                        return value;
                    }
                return std::nullopt;
            });
        };
        using Device = DxrtProfilerSample::NpuDeviceSample;
        write_profiler_row(output, "NPU Input Format Handler",
            device_statistics(&Device::input_format_us));
        write_profiler_row(output, "H2D (PCIe Write)",
            device_statistics(&Device::h2d_us));
        write_profiler_row(output, "NPU Inference",
            device_statistics(&Device::inference_core_all_us));
        write_profiler_row(output, "  - Inference Core 0",
            device_statistics(&Device::inference_core_0_us, true));
        write_profiler_row(output, "  - Inference Core 1",
            device_statistics(&Device::inference_core_1_us, true));
        write_profiler_row(output, "  - Inference Core 2",
            device_statistics(&Device::inference_core_2_us, true));
        write_profiler_row(output, "D2H (PCIe Read)",
            device_statistics(&Device::d2h_us));
        write_profiler_row(output, "NPU Output Format Handler",
            device_statistics(&Device::output_format_us));
        write_profiler_row(output, "NPU Task Total",
            device_statistics(&Device::total_us));
        output << section_separator << '\n';
    }

    output << "[ CPU TASK - cpu_0 / ONNX Runtime ]\n";
    write_profiler_row(output, "CPU Task (total)", sample_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.cpu_task_us; }));
    output << section_separator << '\n';

    output << "[ C++ POSTPROCESS ]\n";
    write_profiler_row(output, "Postprocess", sample_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.postprocess_us; }));
    output << section_separator << '\n';

    output << "[ TRACKING ]\n";
    write_profiler_row(output, "Tracking", sample_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.tracking_us; }));
    output << section_separator << '\n';

    output << "[ LATENCY SUMMARY ]\n";
    write_profiler_row(output, "Host Latency", sample_statistics(
        samples, [](const DxrtProfilerSample& s) { return s.host_latency_us; }));
    output << '\n' << separator << '\n';
    write_profiler_row(output, "TOTAL LATENCY", sample_statistics(
        samples, total_latency_us));

    output << separator << '\n';
}

void write_dxrt_profiler_summary_outputs(
    std::ostream& terminal,
    const std::filesystem::path& file_path,
    const std::string& sequence_name,
    std::size_t frame_count,
    const std::vector<DxrtProfilerSample>& samples)
{
    std::ostringstream summary;
    write_dxrt_profiler_summary(summary, sequence_name, frame_count, samples);
    const std::string text = summary.str();

    std::ofstream file(file_path);
    if (!file)
        throw std::runtime_error(
            "DXRT profiler summary 파일 생성에 실패했습니다: " +
            file_path.string());
    file << text;
    file.close();
    if (file.fail())
        throw std::runtime_error(
            "DXRT profiler summary 저장에 실패했습니다: " +
            file_path.string());
    terminal << text;
}

}  // namespace benchmark_result_writer
