#pragma once

#include <string>

#include "config/app_config.hpp"

// benchmark.yaml과 연결된 모델 YAML을 합친 최상위 설정.
struct BenchmarkConfig
{
    LoggingConfig logging;
    std::string model_config_path;
    ModelConfig model;
    DetectionConfig detection;
    TrackingConfig tracking;
    BenchmarkPathConfig path;
};

BenchmarkConfig load_benchmark_config(const std::string& path);

int run_benchmark(const std::string& config_path);
