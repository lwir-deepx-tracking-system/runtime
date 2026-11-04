#include "runtime_metrics.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
// model/tracker와 자동 판별한 실행 모드 아래에 충돌 없는 timestamp를 만든다.
fs::path RuntimeMetricsWriter::create_directory(
    const fs::path& output_root,
    const std::string& model_name,
    const std::string& tracker_name,
    const std::string& runtime_mode)
{
    const fs::path mode_dir = output_root /
        (model_name + "_" + tracker_name) / runtime_mode;
    fs::create_directories(mode_dir);

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    if (localtime_r(&now_time, &local_time) == nullptr)
        throw std::runtime_error("Runtime timestamp 생성에 실패했습니다");

    std::ostringstream stream;
    stream << std::put_time(&local_time, "%Y%m%d_%H%M%S");
    if (!stream)
        throw std::runtime_error("Runtime timestamp 생성에 실패했습니다");
    const std::string timestamp = stream.str();

    // 같은 초에 시작한 실행은 기존 결과를 보존하며 suffix를 증가시킨다.
    for (std::size_t suffix = 0;; ++suffix)
    {
        const fs::path result_dir = mode_dir /
            (timestamp + (suffix == 0 ? "" : "_" + std::to_string(suffix)));
        if (fs::create_directory(result_dir))
            return result_dir;
    }
}

// 종료된 worker의 측정값을 한 CSV에 모은다.
static void append_metrics(
    std::ofstream& file,
    const char* stage,
    const std::vector<StageMetric>& metrics)
{
    for (const StageMetric& metric : metrics)
    {
        file << metric.frame_id << ',' << stage << ',';
        if (metric.queue_wait_ms)
            file << *metric.queue_wait_ms;
        file << ',' << metric.processing_ms << ',';
        if (metric.e2e_ms)
            file << *metric.e2e_ms;
        file << '\n';
    }
}


// 카메라·검출·추적·제어 시간과 검출 세부 시간을 별도 CSV에 기록한다.
void RuntimeMetricsWriter::write(const fs::path& result_dir,
    const std::vector<StageMetric>& camera,
    const std::vector<StageMetric>& detection,
    const std::vector<StageMetric>& tracking,
    const std::vector<StageMetric>* control,
    std::vector<DetectionTiming> detection_timings)
{
        std::ofstream metrics_file(result_dir / "metrics.csv");
        if (!metrics_file)
            throw std::runtime_error("metrics.csv 파일을 생성하지 못했습니다");

        metrics_file << "frame_id,stage,queue_wait_ms,processing_ms,e2e_ms\n"
                     << std::fixed << std::setprecision(6);
        append_metrics(metrics_file, "camera", camera);
        append_metrics(metrics_file, "detection", detection);
        append_metrics(metrics_file, "tracking", tracking);
        if (control)
            append_metrics(metrics_file, "control_submit", *control);

        if (!metrics_file)
            throw std::runtime_error("metrics.csv 파일 쓰기에 실패했습니다");
        metrics_file.close();
        if (metrics_file.fail())
            throw std::runtime_error("metrics.csv 파일을 저장하지 못했습니다");

        // 모든 Detection callback이 끝난 뒤 프레임별 내부 단계 시간을 저장한다.
        std::sort(
            detection_timings.begin(), detection_timings.end(),
            [](const DetectionTiming& lhs, const DetectionTiming& rhs)
            {
                return lhs.frame_id < rhs.frame_id;
            });

        std::ofstream detection_timing_file(
            result_dir / "detection_timing.csv");
        if (!detection_timing_file)
            throw std::runtime_error(
                "detection_timing.csv 파일을 생성하지 못했습니다");

        detection_timing_file
            << "frame_id,preprocess_ms,latency_ms,"
               "npu_processing_ms,postprocess_ms\n"
            << std::fixed << std::setprecision(6);
        for (const DetectionTiming& timing : detection_timings)
        {
            detection_timing_file
                << timing.frame_id << ','
                << timing.preprocess_ms() << ','
                << timing.latency_ms() << ','
                << timing.npu_processing_ms() << ','
                << timing.postprocess_ms() << '\n';
        }
        if (!detection_timing_file)
            throw std::runtime_error(
                "detection_timing.csv 파일 쓰기에 실패했습니다");
        detection_timing_file.close();
        if (detection_timing_file.fail())
            throw std::runtime_error(
                "detection_timing.csv 파일을 저장하지 못했습니다");

}
