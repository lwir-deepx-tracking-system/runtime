#include "web_gui/video.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace web_gui {
namespace {
constexpr auto kMetadataLifetime = std::chrono::seconds(1);

// Bind the metadata socket before starting the receive worker.
GSocket* bind_metadata(unsigned port)
{
    auto* socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
                                G_SOCKET_PROTOCOL_UDP, nullptr);
    if (!socket) throw std::runtime_error("Cannot open metadata UDP socket");
    auto* ip = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    auto* address = g_inet_socket_address_new(ip, port);
    const bool bound = g_socket_bind(socket, address, false, nullptr);
    g_object_unref(address);
    g_object_unref(ip);
    if (!bound) {
        g_object_unref(socket);
        throw std::runtime_error("Cannot bind metadata UDP port " + std::to_string(port));
    }
    return socket;
}
}

VideoReceiver::VideoReceiver(unsigned video_port, unsigned metadata_port, bool jpeg)
{
    const auto description = "udpsrc address=127.0.0.1 port=" + std::to_string(video_port) +
        (jpeg ? " caps=application/x-rtp,media=video,clock-rate=90000,encoding-name=JPEG,payload=26 "
              : " caps=application/x-rtp,media=video,clock-rate=90000,encoding-name=H264,payload=96 ") +
        "! rtpjitterbuffer latency=60 name=jitter " +
        (jpeg ? "! rtpjpegdepay ! jpegdec " : "! rtph264depay ! avdec_h264 ") +
        "! videoconvert ! video/x-raw,width=640,height=480 ! cairooverlay name=boxes "
        "! videoconvert ! jpegenc ! appsink name=output emit-signals=true sync=false "
        "max-buffers=1 drop=true";
    GError* error = nullptr;
    pipeline_ = gst_parse_launch(description.c_str(), &error);
    try {
        if (error || !pipeline_) {
            const std::string message = error ? error->message : "Could not construct RTP receiver";
            g_clear_error(&error);
            throw std::runtime_error(message);
        }
        metadata_socket_ = bind_metadata(metadata_port);
        auto* jitter = gst_bin_get_by_name(GST_BIN(pipeline_), "jitter");
        auto* pad = gst_element_get_static_pad(jitter, "src");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, rtp, this, nullptr);
        gst_object_unref(pad);
        gst_object_unref(jitter);
        auto* boxes = gst_bin_get_by_name(GST_BIN(pipeline_), "boxes");
        g_signal_connect(boxes, "draw", G_CALLBACK(draw), this);
        gst_object_unref(boxes);
        auto* output = gst_bin_get_by_name(GST_BIN(pipeline_), "output");
        g_signal_connect(output, "new-sample", G_CALLBACK(sample), this);
        gst_object_unref(output);
        if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            throw std::runtime_error("Could not start RTP receiver");
        }
        metadata_worker_ = std::thread(&VideoReceiver::receive_metadata, this);
    } catch (...) {
        if (pipeline_) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
        }
        g_clear_object(&metadata_socket_);
        throw;
    }
}

VideoReceiver::~VideoReceiver()
{
    stopped_ = true;
    changed_.notify_all();
    metadata_worker_.join();
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    g_object_unref(metadata_socket_);
}

void VideoReceiver::receive_metadata()
{
    std::array<std::uint8_t, 65536> packet{};
    while (!stopped_) {
        if (!g_socket_condition_timed_wait(metadata_socket_, G_IO_IN, 100000, nullptr, nullptr))
            continue;
        const auto size = g_socket_receive(metadata_socket_, reinterpret_cast<char*>(packet.data()),
                                           packet.size(), nullptr, nullptr);
        GuiTrackingMetadata metadata;
        if (size <= 0 || !decode_tracking_metadata(packet.data(), size, metadata)) continue;
        const auto now = Clock::now();
        std::lock_guard lock(mutex_);
        for (auto it = metadata_.begin(); it != metadata_.end();) {
            if (now - it->second.first > kMetadataLifetime) it = metadata_.erase(it);
            else ++it;
        }
        if (metadata_.size() >= 120) metadata_.erase(metadata_.begin());
        metadata_[metadata.rtp_timestamp] = {now, std::move(metadata)};
    }
}

GstPadProbeReturn VideoReceiver::rtp(GstPad*, GstPadProbeInfo* info, gpointer userdata)
{
    auto& self = *static_cast<VideoReceiver*>(userdata);
    auto* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    std::array<std::uint8_t, 12> header{};
    if (buffer && GST_BUFFER_PTS_IS_VALID(buffer) &&
        gst_buffer_extract(buffer, 0, header.data(), header.size()) == header.size()) {
        const auto source = read_u32(header.data() + 8);
        std::lock_guard lock(self.mutex_);
        if (self.source_ && self.source_ != source) {
            self.timestamps_.clear();
            self.drawn_.clear();
            self.displayed_.clear();
            self.metadata_.clear();
        }
        self.source_ = source;
        self.timestamps_[GST_BUFFER_PTS(buffer)] = read_u32(header.data() + 4);
        if (self.timestamps_.size() > 120) self.timestamps_.erase(self.timestamps_.begin());
    }
    return GST_PAD_PROBE_OK;
}

void VideoReceiver::draw(GstElement*, cairo_t* context, guint64 pts, guint64, gpointer userdata)
{
    auto& self = *static_cast<VideoReceiver*>(userdata);
    GuiTrackingMetadata metadata;
    {
        std::lock_guard lock(self.mutex_);
        const auto timestamp = self.timestamps_.find(pts);
        if (timestamp == self.timestamps_.end()) return;
        const auto match = self.metadata_.find(timestamp->second);
        if (match == self.metadata_.end() || Clock::now() - match->second.first > kMetadataLifetime)
            return;
        metadata = match->second.second;
        self.drawn_[pts] = metadata;
        if (self.drawn_.size() > 120) self.drawn_.erase(self.drawn_.begin());
    }
    cairo_set_source_rgb(context, 1, 0.68, 0.24);
    cairo_set_line_width(context, 2);
    cairo_set_font_size(context, 14);
    for (const auto& track : metadata.tracks) {
        cairo_rectangle(context, track.x, track.y, track.width, track.height);
        cairo_stroke(context);
        cairo_move_to(context, track.x, std::max(track.y - 3, 14.0F));
        cairo_show_text(context, std::to_string(track.track_id).c_str());
    }
}

GstFlowReturn VideoReceiver::sample(GstAppSink* sink, gpointer userdata)
{
    auto& self = *static_cast<VideoReceiver*>(userdata);
    std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample(
        gst_app_sink_pull_sample(sink), gst_sample_unref);
    if (!sample) return GST_FLOW_EOS;
    auto* buffer = gst_sample_get_buffer(sample.get());
    std::string jpeg(gst_buffer_get_size(buffer), '\0');
    gst_buffer_extract(buffer, 0, jpeg.data(), jpeg.size());
    {
        std::lock_guard lock(self.mutex_);
        GuiTrackingMetadata metadata;
        auto match = self.drawn_.extract(GST_BUFFER_PTS(buffer));
        if (match) {
            metadata = std::move(match.mapped());
            const auto comment = "LWIR k=" + std::to_string(metadata.frame_id);
            const unsigned length = comment.size() + 2;
            std::string marker{char(0xff), char(0xfe), char(length >> 8), char(length)};
            if (jpeg.size() > 2) jpeg.insert(2, marker + comment);
            self.displayed_[metadata.frame_id] = {Clock::now(), metadata};
            if (self.displayed_.size() > 60) self.displayed_.erase(self.displayed_.begin());
        }
        self.latest_ = std::make_shared<Frame>(
            Frame{++self.sequence_, std::move(jpeg), std::move(metadata), Clock::now()});
    }
    self.changed_.notify_all();
    return GST_FLOW_OK;
}

std::shared_ptr<const Frame> VideoReceiver::latest()
{
    std::lock_guard lock(mutex_);
    return latest_;
}

std::optional<GuiTrackingMetadata> VideoReceiver::shown_metadata(std::uint64_t frame_id)
{
    std::lock_guard lock(mutex_);
    const auto frame = displayed_.find(frame_id);
    if (frame == displayed_.end() || Clock::now() - frame->second.first > kMetadataLifetime)
        return std::nullopt;
    return frame->second.second;
}

std::shared_ptr<const Frame> VideoReceiver::next(std::uint64_t after)
{
    std::unique_lock lock(mutex_);
    const bool ready = changed_.wait_for(lock, std::chrono::seconds(2),
        [&] { return stopped_ || (latest_ && latest_->sequence > after); });
    return ready && !stopped_ ? latest_ : nullptr;
}
}
