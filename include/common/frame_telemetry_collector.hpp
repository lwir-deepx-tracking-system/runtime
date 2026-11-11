#pragma once

#include "common/frame_telemetry.hpp"
#include "common/measurement_sink.hpp"

#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>

// Fan out existing measurements to CSV and a bounded telemetry event queue.
// Measurement producers (including the RTP probe) never wait for the consumer.
// GuiSender alone drains/aggregates; no file/network I/O happens in the probe.
class FrameTelemetryCollector final : public IMeasurementSink {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    static constexpr std::size_t Capacity = 256, EventCapacity = 2048;
    explicit FrameTelemetryCollector(IMeasurementSink* csv = nullptr) : csv_(csv) {}
    void activate(std::uint64_t, int) noexcept override;
    void record(std::uint64_t, MetricStage, const StageMetric&) noexcept override;
    void record_detection(std::uint64_t, const DetectionTiming&) noexcept override;
    void bind_tracking(std::uint64_t, std::uint64_t, std::uint64_t, int, TimePoint) noexcept override;
    // Called by GuiSender after pipeline creation, before any appsrc push.
    void start_session();
    void reset_context();
    void gui_begin(std::uint64_t generation, std::uint64_t frame, std::uint64_t epoch,
        TimePoint captured, std::uint32_t timestamp, double queue_ms, double convert_ms,
        TimePoint encode_started, TimePoint now = Clock::now());
    void gui_submitted(std::uint32_t timestamp, double submit_ms, bool ok);
    void rtp_ready(std::uint32_t timestamp, TimePoint completed = Clock::now()) noexcept;
    std::optional<FrameTelemetrySnapshot> latest_before(std::uint64_t frame,
        std::uint64_t generation, std::uint64_t epoch, TimePoint now = Clock::now());
    std::uint64_t dropped_events() const { return dropped_.load(); }
    std::uint64_t expired_frames() const { return expired_; }
private:
    enum class Kind { Activate, Stage, Detection, Bind, Marker };
    struct Event {
        Kind kind = Kind::Stage;
        std::uint64_t generation = 0, frame = 0, epoch = 0;
        int id = 0;
        MetricStage stage = MetricStage::Camera;
        StageMetric metric;
        std::array<double, 4> timing{};
        std::uint32_t mask = 0, timestamp = 0;
        TimePoint at{}, captured{};
    };
    struct Entry {
        FrameTelemetrySnapshot sample;
        TimePoint created{}, captured{}, encode_started{};
        bool bound = false, camera = false, detection = false, tracking = false;
        bool gui = false, submitted = false, marker = false, complete = false;
    };
    void enqueue(const Event&) noexcept;
    void drain(TimePoint);
    void apply(const Event&);
    void expire(TimePoint);
    void complete(Entry&);
    Entry* entry(std::uint64_t generation, std::uint64_t frame, TimePoint);
    static void value(Entry&, unsigned slot, double);
    IMeasurementSink* csv_;
    std::mutex queue_mutex_;
    std::array<Event, EventCapacity> events_{};
    std::size_t queue_head_ = 0, queue_size_ = 0;
    std::atomic<std::uint64_t> dropped_{0};
    std::map<std::uint64_t, Entry> frames_;
    std::map<std::uint32_t, std::uint64_t> rtp_frames_;
    std::uint64_t generation_ = 0, epoch_ = 0, session_ = 0, expired_ = 0, last_published_ = 0;
    int model_id_ = 0;
    TimePoint retired_at_{};
};
