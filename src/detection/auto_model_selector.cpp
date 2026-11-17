#include "detection/auto_model_selector.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
double seconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(a - b).count();
}
bool set(Clock::time_point t) { return t.time_since_epoch().count() != 0; }
float iou(const Track& t, const Detection& d) {
    const float x1 = std::max(t.x, d.x), y1 = std::max(t.y, d.y);
    const float x2 = std::min(t.x + t.width, d.x + d.width);
    const float y2 = std::min(t.y + t.height, d.y + d.height);
    const float area = std::max(0.0F, x2 - x1) * std::max(0.0F, y2 - y1);
    const float total = t.width * t.height + d.width * d.height - area;
    return total > 0 ? area / total : 0;
}
}

AutoModelSelector::AutoModelSelector(Config config) : config_(config) {
    if (config.near_id <= 0 || config.far_id <= 0 || config.near_id == config.far_id ||
        config.far_px <= 0 || config.near_px <= config.far_px || config.hold_s < 0 ||
        config.dwell_s < 0 || config.lost_s < 0 || config.lost_occluded_s < 0 ||
        config.near_backoff_s < 0 || config.near_backoff_max_s < config.near_backoff_s ||
        config.median_s < 0 || config.min_score < 0 || config.min_score > 1 ||
        config.max_px <= 0 || config.fast_hps < 0 || config.defer_s < 0 ||
        config.max_per_minute <= 0)
        throw std::invalid_argument("Invalid automatic near/far model policy");
}

void AutoModelSelector::changed(int active_model_id, TimePoint now) {
    const bool automatic_near = pending_ == config_.near_id && active_model_id == config_.near_id;
    if (current_ != 0) {
        last_switch_ = now;
        switches_.push_back(now);
    }
    current_ = active_model_id;
    pending_ = 0;
    near_seen_ = !automatic_near;
    above_since_ = below_since_ = missing_since_ = {};
    heights_.clear();
    signal_samples_.clear();
    due_since_ = {};
    due_target_ = 0;
}

std::optional<int> AutoModelSelector::want(int target, TimePoint now, bool force, bool fast) {
    if (target == current_ || pending_ || (set(retry_after_) && now < retry_after_))
        return std::nullopt;
    while (!switches_.empty() && seconds(now, switches_.front()) >= 60.0)
        switches_.pop_front();
    if (!force && ((set(last_switch_) && seconds(now, last_switch_) < config_.dwell_s) ||
                   switches_.size() >= static_cast<std::size_t>(config_.max_per_minute) ||
                   (target == config_.near_id && set(near_block_until_) && now < near_block_until_)))
        return std::nullopt;
    if (!force && fast && config_.defer_s > 0) {
        if (due_target_ != target) { due_since_ = {}; due_target_ = target; }
        if (!set(due_since_)) due_since_ = now;
        if (seconds(now, due_since_) < config_.defer_s) return std::nullopt;
    }
    pending_ = target;
    due_since_ = {};
    due_target_ = 0;
    if (target == config_.near_id) near_seen_ = false;
    return target;
}

std::optional<int> AutoModelSelector::observe(const DetectionResult& detections,
                                               const TrackingResult& tracks,
                                               int active_model_id, int followed_id) {
    if (!detections.frame || (active_model_id != config_.near_id &&
                              active_model_id != config_.far_id)) return std::nullopt;
    const TimePoint now = detections.frame->metadata.captured_at;
    if (active_model_id != current_) changed(active_model_id, now);
    if (pending_) return std::nullopt;

    struct Candidate { const Track* track; bool edge; bool partial; };
    std::vector<Candidate> candidates;
    for (const auto& t : tracks.tracks) {
        if (t.class_id != 0 || t.predicted ||
            (followed_id > 0 && t.track_id != followed_id) ||
            (followed_id == 0 && t.confidence < config_.min_score) ||
            t.height <= 0 || t.height > config_.max_px || t.width <= 0) continue;
        const bool edge=t.x <= 3 || t.y <= 3 ||
            t.x + t.width >= detections.frame->image.cols - 3 ||
            t.y + t.height >= detections.frame->image.rows - 3;
        bool partial=false;
        for (const auto& d : detections.detections) {
            if (d.class_id != 0 || d.keypoints.size() != 17 || iou(t, d) < 0.5F)
                continue;
            const auto visible = std::count_if(d.keypoints.begin(), d.keypoints.end(),
                [](const Detection::Keypoint& p) { return p.visibility >= 0.5F; });
            if (visible >= 3 && visible < 12) partial=true;
            break;
        }
        candidates.push_back({&t,edge,partial});
    }
    const bool any_clean=std::any_of(candidates.begin(),candidates.end(),
        [](const Candidate& c) { return !c.edge; });
    std::vector<double> heights;
    int partial_count=0;
    for (const auto& c:candidates) {
        if (followed_id==0 && any_clean && c.edge) continue;
        heights.push_back(c.track->height);
        partial_count+=c.partial ? 1 : 0;
    }
    const bool occluded=(!candidates.empty() && !any_clean) ||
        (!heights.empty() && partial_count*2>static_cast<int>(heights.size()));
    const Track* sole=followed_id>0 && candidates.size()==1 ? candidates.front().track : nullptr;
    if (heights.empty()) {
        heights_.clear();
        signal_samples_.clear();
        due_since_ = {};
        due_target_ = 0;
        above_since_ = below_since_ = {};
        if (!set(missing_since_)) missing_since_ = now;
        const double limit = last_occluded_ ? config_.lost_occluded_s : config_.lost_s;
        if (active_model_id == config_.near_id && seconds(now, missing_since_) >= limit) {
            if (!near_seen_) {
                near_backoff_s_ = std::min(config_.near_backoff_max_s,
                    std::max(near_backoff_s_ * 2.0, config_.near_backoff_s));
                near_block_until_ = now + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(near_backoff_s_));
            }
            return want(config_.far_id, now, true);
        }
        return std::nullopt;
    }
    missing_since_ = {};
    if (active_model_id == config_.near_id) {
        near_seen_ = true;
        near_backoff_s_ = 0.0;
        near_block_until_ = {};
    }
    last_occluded_ = occluded;
    std::sort(heights.begin(), heights.end());
    heights_.emplace_back(now, heights[heights.size() / 2]);
    while (!heights_.empty() && seconds(now, heights_.front().first) > config_.median_s)
        heights_.pop_front();
    std::vector<double> window;
    for (const auto& sample : heights_) window.push_back(sample.second);
    std::sort(window.begin(), window.end());
    const double h = window[window.size() / 2];
    bool fast = false;
    if (sole && followed_id > 0) {
        signal_samples_.push_back({now, sole->height, sole->track_id,
            sole->x + sole->width / 2.0, sole->y + sole->height / 2.0});
        while (signal_samples_.size() > 1 &&
               seconds(now, signal_samples_.front().at) > config_.median_s)
            signal_samples_.pop_front();
        const auto& first = signal_samples_.front();
        const auto& last = signal_samples_.back();
        const double dt = seconds(last.at, first.at);
        if (first.track_id == last.track_id && dt >= 0.2 && h > 0)
            fast = std::hypot(last.cx - first.cx, last.cy - first.cy) /
                dt / h > config_.fast_hps;
    } else signal_samples_.clear();
    if (h < config_.far_px && occluded) {
        above_since_ = below_since_ = {};
        due_since_ = {};
        due_target_ = 0;
    } else if (h < config_.far_px) {
        above_since_ = {};
        if (due_target_ == config_.near_id) { due_since_ = {}; due_target_ = 0; }
        if (!set(below_since_)) below_since_ = now;
        if (seconds(now, below_since_) >= config_.hold_s)
            return want(config_.far_id, now, false, fast);
    } else if (h > config_.near_px || occluded) {
        below_since_ = {};
        if (due_target_ == config_.far_id) { due_since_ = {}; due_target_ = 0; }
        if (!set(above_since_)) above_since_ = now;
        if (seconds(now, above_since_) >= config_.hold_s)
            return want(config_.near_id, now, false, fast);
    } else {
        above_since_ = below_since_ = {};
        due_since_ = {};
        due_target_ = 0;
    }
    return std::nullopt;
}

void AutoModelSelector::request_failed() {
    if (pending_ == config_.near_id) near_seen_ = true;
    pending_ = 0;
    retry_after_ = Clock::now() + std::chrono::seconds(1);
}
