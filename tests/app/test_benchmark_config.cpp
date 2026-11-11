#include "benchmark/benchmark.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <yaml-cpp/yaml.h>

// Benchmark가 외부 ByteTrack 설정을 카탈로그 ID로 선택하는지 검증한다.
int main()
{
    const BenchmarkConfig config = load_benchmark_config("config/benchmark.yaml");
    const auto cwd=std::filesystem::current_path();
    struct RestoreCwd {std::filesystem::path path;~RestoreCwd(){std::filesystem::current_path(path);}} restore{cwd};
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    const auto relocated=load_benchmark_config((cwd/"config/benchmark.yaml").string());
    std::filesystem::current_path(cwd);
    if (relocated.detection.model.path!=config.detection.model.path ||
        relocated.reid.model.path!=config.reid.model.path ||
        relocated.tracking_catalog.at(1).config_path!=config.tracking_catalog.at(1).config_path)
        return 4;
    if (config.detection.model.postprocess.type != "yolo26" ||
        config.detection.inference.max_inflight != 1 ||
        !config.reid_enabled ||
        config.reid.config_path != std::filesystem::absolute("config/reid/repvgg.yaml").lexically_normal().string() ||
        config.reid.model.name != "repvgg" ||
        config.reid.matcher_config_path != std::filesystem::absolute("config/reid/matcher.yaml").lexically_normal().string() ||
        config.reid.matcher.similarity_threshold < 0.59F ||
        config.reid.inference.max_inflight != 1 ||
        config.reid.inference.buffer_count != 0 ||
        config.reid_options.max_crops_per_frame != 2 ||
        config.tracking.type != TrackerType::ByteTrack ||
        config.tracking.track_threshold != 0.5F ||
        config.tracking.match_threshold != 0.8F ||
        config.tracking_catalog.at(1).config_path !=
            std::filesystem::absolute("config/tracking/bytetrack.yaml").lexically_normal().string())
        return 1;

    const auto root = config_parser::load_yaml("config/benchmark.yaml");
    auto selection = YAML::Clone(root["tracking"]);
    selection["id"] = 99;
    try {
        config_parser::select_tracking_config(selection, config.tracking_catalog);
        return 2;
    } catch (const std::runtime_error&) {}

    YAML::Node disabled = YAML::Clone(root);
    disabled["reid"] = YAML::Load("enabled: false");
    const auto disabled_path = std::filesystem::temp_directory_path() /
        ("lwir-benchmark-reid-disabled-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".yaml");
    {
        std::ofstream file(disabled_path);
        file << disabled;
    }
    const BenchmarkConfig without_reid = load_benchmark_config(disabled_path.string());
    std::filesystem::remove(disabled_path);
    if (without_reid.reid_enabled || !without_reid.reid.config_path.empty() ||
        !without_reid.reid.matcher_config_path.empty())
        return 3;
    return 0;
}
