#include "app/raw_recorder.hpp"

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <zlib.h>

struct RawRecorder::Impl {
    struct TrackRows {
        std::uint64_t frame_id = 0;
        std::vector<Track> tracks;
    };
    explicit Impl(std::filesystem::path path) : root(std::move(path)) {}
    std::filesystem::path root, current;
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::deque<FrameContextPtr> queue;
    std::deque<TrackRows> track_queue;
    std::uint64_t dropped_track_results = 0;
    std::uint64_t first_captured_id = 0, last_captured_id = 0;
    std::thread writer;
    RawRecordingStatus state;
    bool stopping = false;

    void write() noexcept {
        const auto file = current / "frames.raw.gz";
        gzFile gzip = gzopen(file.string().c_str(), "wb1");
        std::ofstream times(current / "frames.csv", std::ios::binary);
        std::ofstream tracks(current / "tracks.csv", std::ios::binary);
        if (!gzip || !times || !tracks) {
            std::lock_guard<std::mutex> lock(mutex);
            state.failed = true;
            state.active = false;
            stopping = true;
        }
        if (times) times << "raw_frame,frame_id,capture_steady_ns\n";
        if (tracks) tracks << "frame_id,track_id,class_id,confidence,x,y,width,height,predicted\n";
        while (gzip && times && tracks) {
            FrameContextPtr frame;
            TrackRows rows;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait(lock, [this] { return stopping || !queue.empty() || !track_queue.empty(); });
                if (!track_queue.empty()) {
                    rows = std::move(track_queue.front());
                    track_queue.pop_front();
                } else if (!queue.empty()) {
                    frame = std::move(queue.front());
                    queue.pop_front();
                } else break;
            }
            if (!frame) {
                for (const auto& track : rows.tracks)
                    tracks << rows.frame_id << ',' << track.track_id << ',' << track.class_id
                           << ',' << track.confidence << ',' << track.x << ',' << track.y
                           << ',' << track.width << ',' << track.height << ','
                           << (track.predicted ? 1 : 0) << '\n';
                if (!tracks) {
                    std::lock_guard<std::mutex> lock(mutex);
                    state.failed = true;
                    state.active = false;
                    stopping = true;
                    queue.clear();
                    track_queue.clear();
                    break;
                }
                continue;
            }
            const auto& image = frame->image;
            if (image.type() != CV_16UC1 || image.cols != 640 || image.rows != 480) {
                std::lock_guard<std::mutex> lock(mutex);
                state.failed = true;
                state.active = false;
                stopping = true;
                queue.clear();
                track_queue.clear();
                break;
            }
            bool ok = true;
            for (int y = 0; y < image.rows; ++y)
                ok = ok && gzwrite(gzip, image.ptr(y), image.cols * 2) == image.cols * 2;
            if (!ok) {
                std::lock_guard<std::mutex> lock(mutex);
                state.failed = true;
                state.active = false;
                stopping = true;
                queue.clear();
                track_queue.clear();
                break;
            }
            std::lock_guard<std::mutex> lock(mutex);
            times << state.frames << ',' << frame->metadata.frame_id << ','
                  << std::chrono::duration_cast<std::chrono::nanoseconds>(
                         frame->metadata.captured_at.time_since_epoch()).count() << '\n';
            if (!times) {
                state.failed = true;
                state.active = false;
                stopping = true;
                queue.clear();
                track_queue.clear();
                break;
            }
            ++state.frames;
        }
        if (gzip && gzclose(gzip) != Z_OK) {
            std::lock_guard<std::mutex> lock(mutex);
            state.failed = true;
            state.active = false;
        }
        times.close();
        tracks.close();
        if (times.fail() || tracks.fail()) {
            std::lock_guard<std::mutex> lock(mutex);
            state.failed = true;
            state.active = false;
        }
        std::ofstream summary(current / "recording.txt", std::ios::app);
        summary << "dropped_track_results=" << dropped_track_results << '\n';
        if (!summary) {
            std::lock_guard<std::mutex> lock(mutex);
            state.failed = true;
            state.active = false;
        }
    }
};

RawRecorder::RawRecorder(std::filesystem::path root)
    : impl_(std::make_unique<Impl>(std::move(root))) {}
RawRecorder::~RawRecorder() { stop(); }

bool RawRecorder::start(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state.active || impl_->writer.joinable()) return false;
    try {
        std::filesystem::create_directories(impl_->root);
        const auto now = std::chrono::system_clock::now();
        const auto clock = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
        localtime_r(&clock, &local);
        std::ostringstream base;
        base << "MEASURE_" << std::put_time(&local, "%Y%m%d_%H%M%S");
        std::filesystem::path path;
        for (unsigned n = 0; n < 1000; ++n) {
            path = impl_->root / (base.str() + "_" + std::to_string(n));
            if (std::filesystem::create_directory(path)) break;
            if (n == 999) return false;
        }
        impl_->current = path;
        std::ofstream info(path / "recording.txt", std::ios::binary);
        info << "format=uint16_le\nwidth=640\nheight=480\nname=";
        for (char c : name) if (c >= 32 && c != '\n' && c != '\r') info << c;
        info << '\n';
        if (!info) return false;
        const auto next = impl_->state.session + 1;
        impl_->state = {};
        impl_->queue.clear();
        impl_->track_queue.clear();
        impl_->dropped_track_results = 0;
        impl_->first_captured_id = impl_->last_captured_id = 0;
        impl_->state.session = next;
        impl_->state.active = true;
        impl_->stopping = false;
        impl_->writer = std::thread([this] { impl_->write(); });
        return true;
    } catch (...) {
        impl_->state.active = false;
        impl_->state.failed = true;
        return false;
    }
}

bool RawRecorder::stop() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->state.active && !impl_->writer.joinable()) return false;
        impl_->stopping = true;
        impl_->state.active = false;
    }
    impl_->ready.notify_all();
    if (impl_->writer.joinable()) impl_->writer.join();
    return !impl_->state.failed;
}

void RawRecorder::submit(FrameContextPtr frame) noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->state.active || impl_->stopping) return;
    if (!impl_->first_captured_id) impl_->first_captured_id = frame->metadata.frame_id;
    impl_->last_captured_id = frame->metadata.frame_id;
    if (impl_->queue.size() >= 120) {
        ++impl_->state.dropped;
        return;
    }
    impl_->queue.push_back(std::move(frame));
    impl_->ready.notify_one();
}

void RawRecorder::submit_tracks(TrackingResultPtr result) noexcept {
    if (!result || !result->frame) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->state.active || impl_->stopping) return;
    const auto id = result->frame->metadata.frame_id;
    if (!impl_->first_captured_id || id < impl_->first_captured_id ||
        id > impl_->last_captured_id) return;
    if (impl_->track_queue.size() >= 10000) {
        ++impl_->dropped_track_results;
        return;
    }
    impl_->track_queue.push_back({id, result->tracks});
    impl_->ready.notify_one();
}

RawRecordingStatus RawRecorder::status() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state;
}

std::filesystem::path RawRecorder::directory() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->current;
}
