#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <sstream>
#include <string>

// Bounded samples of capture and tracking publication, independent of GUI transport.
class PipelinePerformance {
public:
    using Clock = std::chrono::steady_clock;

    void capture(std::size_t dropped)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++captured_;
        dropped_ += dropped;
    }

    void publish(Clock::time_point captured_at)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = Clock::now();
        ++published_;
        if (last_publish_ != Clock::time_point{})
            intervals_.add(milliseconds(now - last_publish_));
        last_publish_ = now;
        if (captured_at != Clock::time_point{})
            delays_.add(milliseconds(now - captured_at));
    }

    // Runtime calls this periodically. Sort a snapshot outside the producer lock.
    std::string report()
    {
        const auto now = Clock::now();
        const auto seconds = std::chrono::duration<double>(now - start_).count();
        if (seconds < 5.0) return {};

        Samples intervals, delays;
        std::size_t captured, published, dropped;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            start_ = now;
            captured = captured_;
            published = published_;
            dropped = dropped_;
            captured_ = published_ = dropped_ = 0;
            intervals = intervals_;
            delays = delays_;
            intervals_.size = delays_.size = 0;
        }
        std::ostringstream out;
        out << "[PipelinePerformance] capture_fps=" << captured / seconds
            << " tracking_fps=" << published / seconds
            << " camera_queue_drops=" << dropped;
        if (intervals.size) out << " publication_interval_p95_ms=" << intervals.p95();
        if (delays.size) out << " capture_to_tracking_p95_ms=" << delays.p95();
        return out.str();
    }

private:
    static double milliseconds(Clock::duration duration)
    {
        return std::chrono::duration<double, std::milli>(duration).count();
    }

    struct Samples {
        std::array<double, 1024> values{};
        std::size_t size = 0;

        void add(double value)
        {
            if (size < values.size()) values[size++] = value;
        }

        double p95()
        {
            const auto index = (size * 95 - 1) / 100;
            std::nth_element(values.begin(), values.begin() + index, values.begin() + size);
            return values[index];
        }
    };

    std::mutex mutex_;
    Clock::time_point start_ = Clock::now(); // Only the reporting thread accesses this.
    Clock::time_point last_publish_{};
    std::size_t captured_ = 0, published_ = 0, dropped_ = 0;
    Samples intervals_, delays_;
};
