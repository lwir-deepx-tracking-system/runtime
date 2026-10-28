#include <cstdint>
#include <memory>
#include <cerrno>
#include <chrono>
#include <thread>
#include <map>
#include <set>
#include <string>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <opencv2/core.hpp>

#include "config/app_config.hpp"
#include "common/frame.hpp"
#include "common/track.hpp"
#include "gui/gui_protocol.hpp"
#include "gui/gui_sender.hpp"

namespace {
int open_udp_receiver(std::uint16_t& port)
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        return -1;
    }

    socklen_t address_size = sizeof(address);
    if (::getsockname(
            fd, reinterpret_cast<sockaddr*>(&address), &address_size) != 0)
    {
        ::close(fd);
        return -1;
    }
    port = ntohs(address.sin_port);
    return fd;
}

bool drain(int fd, bool video, std::map<std::uint32_t, GuiTrackingMetadata>& metadata,
           std::set<std::uint32_t>& markers, unsigned payload)
{
    std::uint8_t packet[65536];
    for (;;)
    {
        const auto size = ::recv(fd, packet, sizeof(packet), MSG_DONTWAIT);
        if (size < 0) return errno == EAGAIN || errno == EWOULDBLOCK;
        if (video)
        {
            if (size < 12 || size > 1200 || (packet[0] >> 6) != 2 || (packet[1] & 0x7f) != payload)
                return false;
            const auto ts = (std::uint32_t(packet[4]) << 24) | (std::uint32_t(packet[5]) << 16) |
                            (std::uint32_t(packet[6]) << 8) | packet[7];
            if (packet[1] & 0x80) markers.insert(ts);
        }
        else
        {
            GuiTrackingMetadata decoded;
            if (!decode_tracking_metadata(packet, size, decoded)) return false;
            metadata[decoded.rtp_timestamp] = decoded;
        }
    }
}
}

int main(int argc, char** argv)
{
    const std::string encoder = argc > 1 ? argv[1] : "x264enc";
    const bool jpeg = encoder == "jpegenc" || encoder == "mppjpegenc";
    std::uint16_t video_port = 0, metadata_port = 0;
    const int video_fd = open_udp_receiver(video_port);
    const int metadata_fd = open_udp_receiver(metadata_port);
    if (video_fd < 0 || metadata_fd < 0) return 1;
    GuiVideoConfig video{};
    video.host = "127.0.0.1";
    video.port = video_port;
    video.codec = jpeg ? GuiVideoCodec::JPEG : GuiVideoCodec::H264;
    video.encoder = encoder;
    video.bitrate_kbps = 1000;
    video.fps = 30;
    video.rtp_mtu = 1200;
    GuiMetadataConfig meta{};
    meta.enabled = true;
    meta.host = "127.0.0.1";
    meta.port = metadata_port;
    meta.max_packet_bytes = 1200;
    GuiSender sender(video, meta, 8000, 24000);
    std::map<std::uint32_t, GuiTrackingMetadata> metadata;
    std::set<std::uint32_t> markers;
    const auto drain_both = [&] {
        return drain(video_fd, true, metadata, markers, jpeg ? 26 : 96) &&
               drain(metadata_fd, false, metadata, markers, 0);
    };
    // Empty tracks explicitly test transport, not inference. Include a burst so
    // matching cannot rely on the first metadata packet being the first video.
    for (std::uint64_t i = 0; i < 45; ++i)
    {
        auto frame = std::make_shared<FrameContext>();
        frame->image = cv::Mat(480, 640, CV_16UC1, cv::Scalar(16000 + i * 100));
        frame->metadata.frame_id = i;
        TrackingResult result;
        result.frame = frame;
        if (!sender.send(result, 123456)) return 2;
        if (i < 10) std::this_thread::sleep_for(std::chrono::milliseconds(34));
        if (!drain_both()) return 3;
    }
    if (!sender.finish() || !drain_both()) return 4;
    const auto stats = sender.stats();
    sender.stop();
    ::close(video_fd);
    ::close(metadata_fd);
    if (markers.size() < 5 || stats.encoded_frames != markers.size() ||
        stats.processing_samples != stats.encoded_frames || !stats.encode_rtp_ns ||
        !stats.preprocessing_ns || stats.submitted_frames != 45) return 5;
    for (const auto timestamp : markers)
    {
        const auto found = metadata.find(timestamp);
        if (found == metadata.end()) return 6;
        const auto& decoded = found->second;
        if (decoded.rtp_timestamp != decoded.frame_id * 3000 || decoded.width != 640 ||
            decoded.height != 480 || decoded.gui_started_us != 123456 || !decoded.tracks.empty()) return 7;
    }
    return 0;
}
