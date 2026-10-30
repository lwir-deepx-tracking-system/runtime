#pragma once

#include <string>

#include "config/app_config.hpp"

// capture.yaml 전체 설정
struct CaptureConfig
{
    LoggingConfig logging;
    CaptureDatasetConfig dataset;
    CaptureSequenceConfig sequence;
    CaptureConditionConfig condition;
};

CaptureConfig load_capture_config(const std::string& path);

int run_capture(const std::string& config_path);