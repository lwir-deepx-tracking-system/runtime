#include "common/frame_telemetry_collector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
double ms(FrameTelemetryCollector::TimePoint end, FrameTelemetryCollector::TimePoint start)
{ return std::chrono::duration<double, std::milli>(end - start).count(); }
}

void FrameTelemetryCollector::enqueue(const Event& event) noexcept
{
    std::unique_lock<std::mutex> lock(queue_mutex_, std::try_to_lock);
    if (!lock || queue_size_ == EventCapacity) { ++dropped_; return; }
    events_[(queue_head_ + queue_size_) % EventCapacity] = event;
    ++queue_size_;
}

void FrameTelemetryCollector::activate(std::uint64_t generation, int model) noexcept
{
    if (csv_) csv_->activate(generation, model);
    Event e; e.kind = Kind::Activate; e.generation = generation; e.id = model;
    e.at = Clock::now();
    // Lifecycle events must survive a full queue; only measurement events may drop.
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (queue_size_ == EventCapacity) {
        queue_head_ = (queue_head_ + 1) % EventCapacity; --queue_size_; ++dropped_;
    }
    events_[(queue_head_ + queue_size_) % EventCapacity] = e; ++queue_size_;
}
void FrameTelemetryCollector::record(std::uint64_t generation, MetricStage stage,
    const StageMetric& metric) noexcept
{
    if (csv_) csv_->record(generation, stage, metric);
    Event e; e.generation = generation; e.frame = metric.frame_id;
    e.stage = stage; e.metric = metric; e.at = Clock::now(); enqueue(e);
}
void FrameTelemetryCollector::record_detection(std::uint64_t generation,
    const DetectionTiming& timing) noexcept
{
    if (csv_) csv_->record_detection(generation, timing);
    Event e; e.kind = Kind::Detection; e.generation = generation; e.frame = timing.frame_id;
    // Missing timestamps are not measured zeroes.
    const auto valid = [](TimePoint a, TimePoint b) { return a != TimePoint{} && b >= a; };
    if (valid(timing.preprocess_started_at, timing.preprocess_finished_at)) e.mask |= 1;
    if (valid(timing.inference_started_at, timing.inference_finished_at)) e.mask |= 2;
    if (timing.npu_profiler_valid) e.mask |= 4;
    if (valid(timing.postprocess_started_at, timing.postprocess_finished_at)) e.mask |= 8;
    e.timing = {timing.preprocess_ms(), timing.latency_ms(),
                timing.npu_processing_ms(), timing.postprocess_ms()};
    e.at = Clock::now(); enqueue(e);
}
void FrameTelemetryCollector::bind_tracking(std::uint64_t generation, std::uint64_t frame,
    std::uint64_t epoch, int tracker, TimePoint captured) noexcept
{
    Event e; e.kind = Kind::Bind; e.generation = generation; e.frame = frame;
    e.epoch = epoch; e.id = tracker; e.captured = captured; e.at = Clock::now(); enqueue(e);
}
void FrameTelemetryCollector::value(Entry& e, unsigned slot, double v)
{
    if (std::isfinite(v) && v >= 0 && v <= std::numeric_limits<float>::max()) {
        e.sample.values[slot] = static_cast<float>(v);
        e.sample.valid_mask |= 1U << slot;
    }
}
FrameTelemetryCollector::Entry* FrameTelemetryCollector::entry(
    std::uint64_t generation, std::uint64_t frame, TimePoint now)
{
    if (generation != generation_ || !frame || now < retired_at_) return nullptr;
    auto found = frames_.find(frame);
    if (found != frames_.end()) return found->second.complete ? nullptr : &found->second;
    if (frames_.size() == Capacity) {
        // Eviction is telemetry loss, never a video/network loss count.
        for (auto it = rtp_frames_.begin(); it != rtp_frames_.end();)
            if (it->second == frames_.begin()->first) it = rtp_frames_.erase(it); else ++it;
        frames_.erase(frames_.begin()); ++expired_;
    }
    auto& e = frames_[frame]; e.created = now;
    e.sample.session_id = session_; e.sample.model_generation = generation;
    e.sample.model_id = static_cast<std::uint32_t>(std::max(0, model_id_)); e.sample.frame_id = frame;
    return &e;
}
void FrameTelemetryCollector::complete(Entry& e)
{
    if (!e.complete && e.bound && e.camera && e.detection && e.tracking && e.gui &&
        e.submitted && e.marker && e.sample.model_id && e.sample.tracker_id)
    {
        e.complete = true; // Immutable from this point; optional late control is ignored.
    }
}
void FrameTelemetryCollector::apply(const Event& event)
{
    if (event.kind == Kind::Activate) {
        if (event.generation < generation_) return;
        if (event.generation != generation_ || event.id != model_id_) {
            frames_.clear(); rtp_frames_.clear(); epoch_ = 0; last_published_ = 0;
        }
        generation_ = event.generation; model_id_ = event.id;
        return;
    }
    if (event.at < retired_at_) return;
    if (event.kind == Kind::Marker) {
        const auto mapping = rtp_frames_.find(event.timestamp);
        if (mapping == rtp_frames_.end()) return;
        const auto found = frames_.find(mapping->second);
        if (found != frames_.end() && !found->second.complete) {
            auto& e = found->second;
            value(e, 16, ms(event.at, e.encode_started));
            if (e.captured != TimePoint{}) value(e, 17, ms(event.at, e.captured));
            e.marker = true; complete(e);
        }
        rtp_frames_.erase(mapping); return;
    }
    if (event.generation != generation_) return;
    if (event.kind == Kind::Bind && event.epoch < epoch_) return;
    if (event.kind == Kind::Bind && event.epoch > epoch_) {
        epoch_ = event.epoch; last_published_ = 0;
        for (auto it = frames_.begin(); it != frames_.end();) {
            if (it->second.bound && it->second.sample.tracker_epoch != epoch_) it = frames_.erase(it);
            else ++it;
        }
        rtp_frames_.clear();
    }
    auto* e = entry(event.generation, event.frame, event.at);
    if (!e) return;
    if (event.kind == Kind::Bind) {
        e->bound = true; e->sample.tracker_epoch = event.epoch;
        e->sample.tracker_id = static_cast<std::uint32_t>(std::max(0, event.id));
        e->captured = event.captured;
    } else if (event.kind == Kind::Detection) {
        for (unsigned i = 0; i < 4; ++i) if (event.mask & (1U << i)) value(*e, 3 + i, event.timing[i]);
    } else if (event.kind == Kind::Stage) {
        const auto& m = event.metric;
        switch (event.stage) {
        case MetricStage::Camera: value(*e, 0, m.processing_ms); e->camera = true; break;
        case MetricStage::Detection:
            if (m.queue_wait_ms) value(*e, 1, *m.queue_wait_ms);
            value(*e, 2, m.processing_ms); e->detection = true; break;
        case MetricStage::Tracking:
            if (m.queue_wait_ms) value(*e, 7, *m.queue_wait_ms);
            value(*e, 8, m.processing_ms);
            if (m.e2e_ms) value(*e, 9, *m.e2e_ms);
            e->tracking = true; break;
        case MetricStage::Control:
            if (m.queue_wait_ms) value(*e, 10, *m.queue_wait_ms);
            value(*e, 11, m.processing_ms);
            if (m.e2e_ms) value(*e, 12, *m.e2e_ms);
            break;
        }
    }
    complete(*e);
}
void FrameTelemetryCollector::expire(TimePoint now)
{
    for (auto it = frames_.begin(); it != frames_.end();) {
        if (now - it->second.created >= std::chrono::seconds(2)) { it = frames_.erase(it); ++expired_; }
        else ++it;
    }
    for (auto it = rtp_frames_.begin(); it != rtp_frames_.end();)
        if (!frames_.count(it->second)) it = rtp_frames_.erase(it); else ++it;
}
void FrameTelemetryCollector::drain(TimePoint now)
{
    // A fixed upper bound also prevents a producer flood starving video submission.
    for (std::size_t i = 0; i < EventCapacity; ++i) {
        Event event;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (!queue_size_) break;
            event = events_[queue_head_]; queue_head_ = (queue_head_ + 1) % EventCapacity; --queue_size_;
        }
        apply(event);
    }
    expire(now);
}
void FrameTelemetryCollector::start_session()
{
    static std::atomic<std::uint64_t> sequence{static_cast<std::uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count()) | 1U};
    drain(Clock::now()); session_ = ++sequence;
    frames_.clear(); rtp_frames_.clear(); last_published_ = 0; retired_at_ = Clock::now();
}
void FrameTelemetryCollector::reset_context()
{
    frames_.clear(); rtp_frames_.clear(); last_published_ = 0; retired_at_ = Clock::now();
}
void FrameTelemetryCollector::gui_begin(std::uint64_t generation, std::uint64_t frame,
    std::uint64_t epoch, TimePoint captured, std::uint32_t timestamp,
    double queue_ms, double convert_ms, TimePoint encode_started, TimePoint now)
{
    drain(now);
    auto* e = entry(generation, frame, now);
    if (!e || !session_ || epoch != epoch_) return;
    e->gui = true; e->captured = captured; e->encode_started = encode_started;
    value(*e, 13, queue_ms); value(*e, 14, convert_ms);
    rtp_frames_[timestamp] = frame;
}
void FrameTelemetryCollector::gui_submitted(std::uint32_t timestamp, double submit_ms, bool ok)
{
    // Marker may precede push return. Save the frame key before draining markers.
    const auto mapping = rtp_frames_.find(timestamp);
    if (mapping == rtp_frames_.end()) return;
    const auto frame = mapping->second;
    drain(Clock::now());
    const auto found = frames_.find(frame);
    if (found == frames_.end()) return;
    if (!ok) { frames_.erase(found); rtp_frames_.erase(timestamp); return; }
    value(found->second, 15, submit_ms); found->second.submitted = true; complete(found->second);
}
void FrameTelemetryCollector::rtp_ready(std::uint32_t timestamp, TimePoint completed) noexcept
{
    Event e; e.kind = Kind::Marker; e.timestamp = timestamp; e.at = completed; enqueue(e);
}
std::optional<FrameTelemetrySnapshot> FrameTelemetryCollector::latest_before(
    std::uint64_t frame, std::uint64_t generation, std::uint64_t epoch, TimePoint now)
{
    drain(now);
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        const auto& e = it->second;
        if (e.complete && e.sample.frame_id >= last_published_ && e.sample.frame_id < frame && e.sample.model_generation == generation &&
            e.sample.tracker_epoch == epoch && e.captured != TimePoint{}) {
            auto sample = e.sample; sample.sample_age_ms = static_cast<float>(ms(now, e.captured));
            if (std::isfinite(sample.sample_age_ms) && sample.sample_age_ms >= 0) {
                last_published_ = sample.frame_id; return sample;
            }
        }
    }
    return std::nullopt;
}
