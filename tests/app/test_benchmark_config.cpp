#include "benchmark/benchmark.hpp"

#include <stdexcept>
#include <yaml-cpp/yaml.h>

// Benchmark가 외부 ByteTrack 설정을 카탈로그 ID로 선택하는지 검증한다.
int main()
{
    const BenchmarkConfig config = load_benchmark_config("config/benchmark.yaml");
    if (config.detection.model.postprocess.type != "yolo26" ||
        config.detection.inference.max_inflight != 1 ||
        config.reid.config_path != "config/reid/repvgg.yaml" ||
        config.reid.model.name != "repvgg" ||
        config.reid.matcher_config_path != "config/reid/matcher.yaml" ||
        config.reid.matcher.similarity_threshold < 0.59F ||
        config.reid.inference.max_inflight != 1 ||
        config.reid.inference.buffer_count != 0 ||
        config.reid_options.max_crops_per_frame != 2 ||
        config.tracking.type != TrackerType::ByteTrack ||
        config.tracking.track_threshold != 0.5F ||
        config.tracking.match_threshold != 0.8F ||
        config.tracking_catalog.at(1).config_path !=
            "config/tracking/bytetrack.yaml")
        return 1;

    const auto root = config_parser::load_yaml("config/benchmark.yaml");
    auto selection = YAML::Clone(root["tracking"]);
    selection["id"] = 99;
    try {
        config_parser::select_tracking_config(selection, config.tracking_catalog);
        return 2;
    } catch (const std::runtime_error&) {}
    return 0;
}
