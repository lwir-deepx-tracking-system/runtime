#pragma once
#include "app/pipeline_requests.hpp"
#include "control/gimbal_command_executor.hpp"
#include "detection/auto_model_selector.hpp"
#include "pipeline/tracking_thread.hpp"
#include <cmath>
#include <filesystem>
#include <limits>
#include <mutex>

// Consumes observations and routes automatic requests on the runtime thread.
class AutoModelCoordinator {
    const RuntimeConfig& config_;
    std::uint64_t applied_generation_ = 0;
    ThreadSafeQueue<AutoModelObservation> auto_observation_queue_;
    std::unique_ptr<AutoModelSelector> auto_selector_;
    // GUI는 생성 시 확정된 가용 여부만 읽고 selector 소유권은 runtime 루프가 관리한다.
    bool auto_selector_available_ = false;
    mutable std::mutex settings_mutex_;
    bool auto_detection_enabled_ = false;
    float auto_far_px_ = 100.0f, auto_near_px_ = 140.0f;
    std::uint64_t auto_settings_generation_ = 0;
    std::shared_future<PipelineResult> auto_completion_;

public:
    explicit AutoModelCoordinator(const RuntimeConfig& config) : config_(config)
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

    ThreadSafeQueue<AutoModelObservation>* observations()
    {
        return auto_selector_available_ ? &auto_observation_queue_ : nullptr;
    }

    PipelineAutoDetection auto_detection() const {
        const bool present=auto_selector_available_ &&
            std::filesystem::is_regular_file(config_.detection_catalog.at(config_.auto_near_model_id).model.path) &&
            std::filesystem::is_regular_file(config_.detection_catalog.at(config_.auto_far_model_id).model.path);
        std::lock_guard<std::mutex> lock(settings_mutex_);
        return {auto_detection_enabled_, present,
            static_cast<std::uint32_t>(config_.auto_near_model_id),
            static_cast<std::uint32_t>(config_.auto_far_model_id),
            auto_far_px_, auto_near_px_};
    }

    bool set_auto_detection(bool enabled) {
        if(enabled && !auto_detection().available)return false;
        std::lock_guard<std::mutex> lock(settings_mutex_);
        auto_detection_enabled_ = enabled;
        ++auto_settings_generation_;
        return true;
    }

    bool set_auto_detection_thresholds(float far_px,float near_px) {
        if(!std::isfinite(far_px) || !std::isfinite(near_px) || far_px<20 ||
           near_px<=far_px || near_px>1000)return false;
        std::lock_guard<std::mutex> lock(settings_mutex_);
        auto_far_px_ = far_px;
        auto_near_px_ = near_px;
        ++auto_settings_generation_;
        return true;
    }


    template<class Request>
    void poll(const RuntimeControl& runtime, GimbalCommandExecutor* gimbal, int active_model_id,
        Request request_model_change)
    {
        bool enabled;
        {
            std::lock_guard<std::mutex> lock(settings_mutex_);
            enabled = auto_detection_enabled_;
            if (auto_selector_ && applied_generation_ != auto_settings_generation_) {
                auto_selector_ = std::make_unique<AutoModelSelector>(AutoModelSelector::Config{
                    config_.auto_near_model_id, config_.auto_far_model_id, auto_far_px_, auto_near_px_});
                applied_generation_ = auto_settings_generation_;
            }
        }
        if (auto_selector_) {
            if (auto_completion_.valid() &&
                auto_completion_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                if (auto_completion_.get() != PipelineResult::Ok)
                    auto_selector_->request_failed();
                auto_completion_ = {};
            }
            AutoModelObservation observation;
            while (auto_observation_queue_.try_pop(observation)) {
                const auto control = gimbal ? gimbal->snapshot() : ControlSnapshot{};
                const int followed_id = control.automatic &&
                    control.selected_id<=static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ?
                    static_cast<int>(control.selected_id) : 0;
                if (!enabled || auto_completion_.valid() || !observation.tracks ||
                    observation.detections.model_generation != runtime.generation.load() ||
                    observation.tracks->tracker_epoch != runtime.tracker_epoch.load()) continue;
                const auto target = auto_selector_->observe(
                    observation.detections, *observation.tracks,
                    active_model_id, followed_id);
                if (target) {
                    auto ticket = request_model_change(static_cast<std::uint32_t>(*target));
                    if (ticket.result == PipelineResult::Ok)
                        auto_completion_ = std::move(ticket.completion);
                    else auto_selector_->request_failed();
                }
            }
        }
    }
};
