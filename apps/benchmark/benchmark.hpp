#pragma once

#include <string>
#include <map>

#include "config/app_config.hpp"

// benchmark.yaml과 연결된 모델 YAML을 합친 최상위 설정.
struct BenchmarkConfig
{
    LoggingConfig logging;
    DetectionSettings detection;
    bool reid_enabled = true;
    ReidSettings reid;
    ReidBenchmarkConfig reid_options;
    std::map<int, TrackingCatalogEntry> tracking_catalog;
    TrackingConfig tracking;
    BenchmarkSequenceConfig sequence;
    BenchmarkPathConfig path;
};

BenchmarkConfig load_benchmark_config(const std::string& path);

int run_benchmark(const std::string& config_path);
