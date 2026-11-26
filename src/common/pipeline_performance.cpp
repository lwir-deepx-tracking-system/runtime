#include "common/pipeline_performance.hpp"

#include <algorithm>
#include <sstream>

void PipelinePerformance::capture(std::size_t dropped)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++captured_;
    dropped_ += dropped;
}

void PipelinePerformance::publish(Clock::time_point captured_at)
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

std::string PipelinePerformance::report()
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

double PipelinePerformance::milliseconds(Clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

void PipelinePerformance::Samples::add(double value)
{
    if (size < values.size()) values[size++] = value;
}

double PipelinePerformance::Samples::p95()
{
    const auto index = (size * 95 - 1) / 100;
    std::nth_element(values.begin(), values.begin() + index, values.begin() + size);
    return values[index];
}
