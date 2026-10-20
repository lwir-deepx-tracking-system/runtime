#include "result_writer.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

bool all_lines_have_width(const std::string& text, std::size_t expected_width)
{
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
    {
        if (line.size() != expected_width)
        {
            std::cerr << "expected width " << expected_width
                      << ", actual width " << line.size() << '\n';
            return false;
        }
    }
    return true;
}

}  // namespace

int main()
{
    Detection detection;
    detection.class_id = 2;
    detection.confidence = 0.9213f;
    detection.x = 120.5f;
    detection.y = 75.2f;
    detection.width = 42.1f;
    detection.height = 105.8f;

    std::ostringstream detections;
    benchmark_result_writer::write_detection_summary_header(detections);
    benchmark_result_writer::write_detection_summary_rows(
        detections, 1, {detection});
    benchmark_result_writer::write_summary_footer(detections);

    const std::string detection_text = detections.str();
    if (detection_text.find("frame_id") == std::string::npos ||
        detection_text.find("class_id") == std::string::npos ||
        detection_text.find("0.921300") == std::string::npos ||
        detection_text.find("120.500000") == std::string::npos ||
        !all_lines_have_width(detection_text, 112))
    {
        std::cerr << detection_text;
        return 1;
    }

    Track track;
    track.track_id = 17;
    track.confidence = 0.875f;
    track.x = 9.5f;
    track.y = 10.25f;
    track.width = 30.0f;
    track.height = 40.0f;

    std::ostringstream tracks;
    benchmark_result_writer::write_track_summary_header(tracks);
    benchmark_result_writer::write_track_summary_rows(tracks, 23, {track});
    benchmark_result_writer::write_summary_footer(tracks);

    const std::string track_text = tracks.str();
    if (track_text.find("frame_id") == std::string::npos ||
        track_text.find("track_id") == std::string::npos ||
        track_text.find("0.875000") == std::string::npos ||
        track_text.find("17") == std::string::npos ||
        !all_lines_have_width(track_text, 112))
    {
        std::cerr << track_text;
        return 2;
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
        return 3;
    }

    return 0;
}
