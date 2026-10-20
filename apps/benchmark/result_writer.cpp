#include "result_writer.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <ostream>
#include <string>

namespace benchmark_result_writer {
namespace {

constexpr int kFrameIdWidth = 10;
constexpr int kIdWidth = 10;
constexpr int kValueWidth = 14;
constexpr int kProfilerNameWidth = 29;
constexpr int kProfilerValueWidth = 12;

void write_separator(std::ostream& file, int column_count)
{
    const int width =
        kFrameIdWidth + kIdWidth + (column_count - 2) * kValueWidth +
        3 * column_count + 1;
    file << std::string(width, '-') << '\n';
}

void write_header(
    std::ostream& file,
    const char* id_name,
    bool confidence_first)
{
    write_separator(file, 7);
    file << "| " << std::left << std::setw(kFrameIdWidth) << "frame_id"
         << " | " << std::setw(kIdWidth) << id_name;
    if (confidence_first)
        file << " | " << std::setw(kValueWidth) << "confidence";
    file << " | " << std::setw(kValueWidth) << "x"
         << " | " << std::setw(kValueWidth) << "y"
         << " | " << std::setw(kValueWidth) << "width"
         << " | " << std::setw(kValueWidth) << "height";
    if (!confidence_first)
        file << " | " << std::setw(kValueWidth) << "confidence";
    file << " |\n";
    write_separator(file, 7);
}

void write_detection_row(
    std::ostream& file,
    std::uint64_t frame_id,
    int id,
    float confidence,
    float x,
    float y,
    float width,
    float height)
{
    file << std::fixed << std::setprecision(6)
         << "| " << std::right << std::setw(kFrameIdWidth) << frame_id
         << " | " << std::setw(kIdWidth) << id
         << " | " << std::setw(kValueWidth) << confidence
         << " | " << std::setw(kValueWidth) << x
         << " | " << std::setw(kValueWidth) << y
         << " | " << std::setw(kValueWidth) << width
         << " | " << std::setw(kValueWidth) << height
         << " |\n";
}

void write_track_row(
    std::ostream& file,
    std::uint64_t frame_id,
    const Track& track)
{
    file << std::fixed << std::setprecision(6)
         << "| " << std::right << std::setw(kFrameIdWidth) << frame_id
         << " | " << std::setw(kIdWidth) << track.track_id
         << " | " << std::setw(kValueWidth) << track.x
         << " | " << std::setw(kValueWidth) << track.y
         << " | " << std::setw(kValueWidth) << track.width
         << " | " << std::setw(kValueWidth) << track.height
         << " | " << std::setw(kValueWidth) << track.confidence
         << " |\n";
}

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

void write_detection_summary_header(std::ostream& file)
{
    write_header(file, "class_id", true);
}

void write_detection_summary_rows(
    std::ostream& file,
    std::uint64_t frame_id,
    const std::vector<Detection>& detections)
{
    for (const Detection& detection : detections)
    {
        write_detection_row(
            file,
            frame_id,
            detection.class_id,
            detection.confidence,
            detection.x,
            detection.y,
            detection.width,
            detection.height);
    }
}

void write_track_summary_header(std::ostream& file)
{
    write_header(file, "track_id", false);
}

void write_track_summary_rows(
    std::ostream& file,
    std::uint64_t frame_id,
    const std::vector<Track>& tracks)
{
    for (const Track& track : tracks)
    {
        write_track_row(file, frame_id, track);
    }
}

void write_summary_footer(std::ostream& file)
{
    write_separator(file, 7);
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
