#pragma once

#include <string>

#include "config/app_config.hpp"

// runtime.yaml과 연결된 모델 YAML을 합친 최상위 설정.
struct RuntimeConfig
{
    LoggingConfig logging;
    std::string model_config_path;
    ModelConfig model;
    DetectionConfig detection;
    TrackingConfig tracking;
    ControlConfig control;
    MeasurementConfig measurement;
    GuiConfig gui;
};

RuntimeConfig load_runtime_config(const std::string& path);

int run_runtime(const std::string& config_path);
