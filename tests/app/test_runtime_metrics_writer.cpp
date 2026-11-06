#include "app/metrics_writer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
namespace {
std::vector<std::string> lines(const fs::path& path) {
    std::ifstream file(path);
    std::vector<std::string> result;
    std::string line;
    while (std::getline(file, line)) result.push_back(line);
    return result;
}
}

// A→B→A append, 늦은 이전 세대 결과, 10,000행 제한 제거와 flush 실패를 검증한다.
int main() {
    const auto root = fs::temp_directory_path() /
        ("lwir-writer-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
    const auto config_file = root / "input.yaml";
    { std::ofstream file(config_file); file << "snapshot: true\n"; }
    MetricsWriterOptions options;
    options.output_root = root / "results" / "core";
    options.model_configs = {{1, config_file}, {2, config_file}};
    options.snapshots = {{config_file, "runtime.yaml"}, {config_file, "tracking/1.yaml"}};
    const auto started = std::chrono::steady_clock::now();
    MetricsWriter writer(options);
    writer.activate(0, 1);
    for (unsigned frame = 1; frame <= 10050; ++frame) {
        StageMetric metric; metric.frame_id = frame; metric.processing_ms = 1.5;
        writer.record(0, MetricStage::Detection, metric);
    }
    DetectionTiming timing; timing.frame_id = 1;
    writer.record_detection(0, timing);
    writer.activate(1, 2);
    StageMetric metric; metric.frame_id = 50001;
    writer.record(1, MetricStage::Tracking, metric);
    timing.frame_id = 50001; writer.record_detection(1, timing);
    // 이전 모델의 제어 처리가 늦게 완료돼도 B 파일에 섞이지 않아야 한다.
    metric.frame_id = 60001; writer.record(0, MetricStage::Control, metric);
    writer.activate(2, 1);
    metric.frame_id = 60002; writer.record(2, MetricStage::Camera, metric);
    timing.frame_id = 60002; writer.record_detection(2, timing);
    writer.flush();
    const auto directory = writer.directory();
    const auto a = lines(directory / "models/1/metrics.csv");
    const auto b = lines(directory / "models/2/metrics.csv");
    if (a.size() != 10053 || b.size() != 2 ||
        a[10051].find("60001,control_submit,") != 0 ||
        a[10052].find("60002,camera,") != 0 || b[1].find("50001,tracking,") != 0) return 1;
    if (lines(directory / "models/1/detection_timing.csv").size() != 3 ||
        lines(directory / "models/2/detection_timing.csv").size() != 2) return 2;
    for (std::size_t i = 1; i < a.size(); ++i)
        if (a[i].find("frame_id") != std::string::npos) return 3;
    if (!fs::exists(directory / "runtime.yaml") || !fs::exists(directory / "tracking/1.yaml") ||
        !fs::exists(directory / "models/2/model.yaml")) return 4;
    writer.close(); writer.close();
    if (writer.failed()) return 5;
    std::cout << "10050 rows + model switches: " <<
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << " ms\n";
    MetricsWriter invalid(options);
    if (invalid.directory() == directory) return 6;
    invalid.activate(0, 999);
    try { invalid.flush(); return 7; } catch (const std::exception&) {}
    invalid.close();
    if (!invalid.failed() || invalid.error_message().empty()) return 8;
    if (!fs::exists(directory / "models/1/metrics.csv")) return 9;
    // SIGQUIT 경로는 이미 flush한 현재 실행 파일도 지우고 이전 실행은 보존한다.
    MetricsWriter discarded(options);
    discarded.activate(0, 1);
    discarded.record(0, MetricStage::Detection, metric);
    discarded.flush();
    const auto discarded_directory = discarded.directory();
    if (!fs::exists(discarded_directory / "models/1/metrics.csv")) return 10;
    for (int i = 0; i < 1000; ++i) discarded.record(0, MetricStage::Tracking, metric);
    discarded.discard();
    discarded.record(0, MetricStage::Control, metric); // 종료 중 늦은 제출은 무시한다.
    discarded.activate(1, 2);
    discarded.close(); discarded.discard();
    if (fs::exists(discarded_directory) || discarded.failed() ||
        !fs::exists(directory / "models/1/metrics.csv")) return 11;
    return 0;
}
