#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>

// Bounded samples of capture and tracking publication, independent of GUI transport.
class PipelinePerformance {
public:
    using Clock = std::chrono::steady_clock;

    void capture(std::size_t dropped);

    void publish(Clock::time_point captured_at);

    // Runtime calls this periodically. Sort a snapshot outside the producer lock.
    std::string report();

private:
    static double milliseconds(Clock::duration duration);

    struct Samples {
        std::array<double, 1024> values{};
        std::size_t size = 0;

        void add(double value);

        double p95();
    };

    std::mutex mutex_;
    Clock::time_point start_ = Clock::now(); // Only the reporting thread accesses this.
    Clock::time_point last_publish_{};
    std::size_t captured_ = 0, published_ = 0, dropped_ = 0;
    Samples intervals_, delays_;
};
