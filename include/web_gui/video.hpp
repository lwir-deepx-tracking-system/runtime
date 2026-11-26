#pragma once

#include "web_gui/lan.hpp"

#include <cairo.h>
#include <gst/app/gstappsink.h>
#include <atomic>
#include <condition_variable>
#include <thread>

namespace web_gui {
struct Frame {
    std::uint64_t sequence;
    std::string jpeg;
    GuiTrackingMetadata metadata;
    Clock::time_point received;
};

// Combines board RTP and matching metadata and publishes one latest JPEG.
class VideoReceiver {
public:
    VideoReceiver(unsigned video_port, unsigned metadata_port, bool jpeg);
    ~VideoReceiver();

    std::shared_ptr<const Frame> latest();
    std::shared_ptr<const Frame> next(std::uint64_t after);
    std::optional<GuiTrackingMetadata> shown_metadata(std::uint64_t frame_id);

private:
    void receive_metadata();
    static GstPadProbeReturn rtp(GstPad*, GstPadProbeInfo*, gpointer);
    static void draw(GstElement*, cairo_t*, guint64, guint64, gpointer);
    static GstFlowReturn sample(GstAppSink*, gpointer);

    GSocket* metadata_socket_ = nullptr;
    GstElement* pipeline_ = nullptr;
    std::atomic_bool stopped_{false};
    std::thread metadata_worker_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::map<GstClockTime, std::uint32_t> timestamps_;
    std::optional<std::uint32_t> source_;
    std::map<std::uint32_t, std::pair<Clock::time_point, GuiTrackingMetadata>> metadata_;
    std::map<GstClockTime, GuiTrackingMetadata> drawn_;
    std::shared_ptr<const Frame> latest_;
    std::map<std::uint64_t, std::pair<Clock::time_point, GuiTrackingMetadata>> displayed_;
    std::uint64_t sequence_ = 0;
};
}
