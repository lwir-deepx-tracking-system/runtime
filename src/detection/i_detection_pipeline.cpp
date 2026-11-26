#include "detection/i_detection_pipeline.hpp"

NpuFeedState IDetectionPipeline::npu_feed_state() const { return {}; }

bool IDetectionPipeline::set_npu_feed(std::uint8_t) { return false; }
