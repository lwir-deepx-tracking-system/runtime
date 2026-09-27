#pragma once

#include <string>


struct LoggingConfig
{
    std::string level;
};


struct DetectorConfig
{
    std::string type;
    std::string backend;
    std::string config_path;
};


struct TrackingConfig
{
    std::string type;
    std::string config_path;
};


struct ControlConfig
{
    bool enabled;
    std::string config_path;
};

struct MeasurementConfig
{
    bool enabled = false;
};


struct AppConfig
{
    LoggingConfig logging;

    DetectorConfig detector;
    TrackingConfig tracking;
    ControlConfig control;
    MeasurementConfig measurement;
};


AppConfig load_config(const std::string& config_path);
