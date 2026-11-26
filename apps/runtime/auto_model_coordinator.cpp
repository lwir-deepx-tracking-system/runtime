#include "runtime/auto_model_coordinator.hpp"

#include <cmath>
#include <filesystem>

AutoModelCoordinator::AutoModelCoordinator(const RuntimeConfig& config) : config_(config)
{
    if (config_.auto_near_model_id>0 && config_.auto_far_model_id>0 &&
        config_.detection_catalog.count(config_.auto_near_model_id) &&
        config_.detection_catalog.count(config_.auto_far_model_id)) {
        auto_selector_ = std::make_unique<AutoModelSelector>(
            AutoModelSelector::Config{config_.auto_near_model_id, config_.auto_far_model_id});
        auto_selector_available_ = true;
        auto_detection_enabled_ = config_.auto_detection_enabled;
    }
}

ThreadSafeQueue<AutoModelObservation>* AutoModelCoordinator::observations()
{
    return auto_selector_available_ ? &auto_observation_queue_ : nullptr;
}

PipelineAutoDetection AutoModelCoordinator::auto_detection() const {
    const bool present=auto_selector_available_ &&
        std::filesystem::is_regular_file(config_.detection_catalog.at(config_.auto_near_model_id).model.path) &&
        std::filesystem::is_regular_file(config_.detection_catalog.at(config_.auto_far_model_id).model.path);
    std::lock_guard<std::mutex> lock(settings_mutex_);
    return {auto_detection_enabled_, present,
        static_cast<std::uint32_t>(config_.auto_near_model_id),
        static_cast<std::uint32_t>(config_.auto_far_model_id),
        auto_far_px_, auto_near_px_};
}

bool AutoModelCoordinator::set_auto_detection(bool enabled) {
    if(enabled && !auto_detection().available)return false;
    std::lock_guard<std::mutex> lock(settings_mutex_);
    auto_detection_enabled_ = enabled;
    ++auto_settings_generation_;
    return true;
}

bool AutoModelCoordinator::set_auto_detection_thresholds(float far_px,float near_px) {
    if(!std::isfinite(far_px) || !std::isfinite(near_px) || far_px<20 ||
       near_px<=far_px || near_px>1000)return false;
    std::lock_guard<std::mutex> lock(settings_mutex_);
    auto_far_px_ = far_px;
    auto_near_px_ = near_px;
    ++auto_settings_generation_;
    return true;
}
