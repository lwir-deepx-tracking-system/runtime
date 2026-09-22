#pragma once

#include <string>


struct LoggingConfig
{
    std::string level;
};


struct DetectorConfig
{
    std::string type;
    std::string config_path;
};


struct PreprocessConfig
{
    std::string backend;
};


struct InferenceConfig
{
    std::string backend;
};


struct PostprocessConfig
{
    std::string backend;
};


struct TrackingConfig
{
    std::string type;
    std::string config_path;
};


struct CommunicationConfig
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

    PreprocessConfig preprocess;
    InferenceConfig inference;
    PostprocessConfig postprocess;

    TrackingConfig tracking;
    CommunicationConfig communication;
    MeasurementConfig measurement;
};


AppConfig load_config(const std::string& config_path);
