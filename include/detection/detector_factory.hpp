#pragma once
#include <memory>
#include "config/app_config.hpp"
#include "detection/i_detection_pipeline.hpp"
class DetectorFactory { public: static std::unique_ptr<IDetectionPipeline> create(const ModelConfig&, const DetectionConfig&); };
