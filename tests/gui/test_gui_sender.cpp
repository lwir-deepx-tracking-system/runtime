#include <cstdint>
#include <memory>

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

bool wait_for_udp(int fd)
{
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = POLLIN;
    return ::poll(&descriptor, 1, 2000) > 0;
}
}

int main()
{
    std::uint16_t video_port = 0;
    std::uint16_t metadata_port = 0;
    const int video_fd = open_udp_receiver(video_port);
    const int metadata_fd = open_udp_receiver(metadata_port);
    if (video_fd < 0 || metadata_fd < 0)
    {
        if (video_fd >= 0) ::close(video_fd);
        if (metadata_fd >= 0) ::close(metadata_fd);
        return 1;
    }

    GuiVideoConfig video{};
    video.host = "127.0.0.1";
    video.port = video_port;
    video.codec = GuiVideoCodec::H264;
    video.encoder = "x264enc";
    video.bitrate_kbps = 1000;
    video.fps = 30;
    video.rtp_mtu = 1200;

    GuiMetadataConfig metadata{};
    metadata.enabled = true;
    metadata.host = "127.0.0.1";
    metadata.port = metadata_port;
    metadata.max_packet_bytes = 1200;

    GuiSender sender(video, metadata, 8000, 24000);

    // 실제 inference 결과를 만들지 않고 CV_16UC1 frame과 빈 Track 목록으로
    // 박스 없는 영상 RTP와 별도 metadata UDP 경로를 검증한다.
    auto frame = std::make_shared<FrameContext>();
    frame->image = cv::Mat(480, 640, CV_16UC1, cv::Scalar(16000));

    TrackingResult result;
    result.frame = frame;
    for (std::uint64_t frame_id = 0; frame_id < 3; ++frame_id)
    {
        frame->metadata.frame_id = frame_id;
        if (!sender.send(result))
        {
            sender.stop();
            ::close(video_fd);
            ::close(metadata_fd);
            return 2;
        }
    }

    if (!wait_for_udp(video_fd) || !wait_for_udp(metadata_fd))
    {
        sender.stop();
        ::close(video_fd);
        ::close(metadata_fd);
        return 3;
    }

    std::uint8_t video_packet[2048]{};
    const ssize_t video_size = ::recv(
        video_fd, video_packet, sizeof(video_packet), 0);

    std::uint8_t metadata_packet[2048]{};
    const ssize_t metadata_size = ::recv(
        metadata_fd, metadata_packet, sizeof(metadata_packet), 0);

    sender.stop();
    ::close(video_fd);
    ::close(metadata_fd);

    if (video_size < 12 || (video_packet[0] >> 6U) != 2U ||
        (video_packet[1] & 0x7fU) != 96U)
        return 4;

    GuiTrackingMetadata decoded;
    if (metadata_size <= 0 ||
        !decode_tracking_metadata(
            metadata_packet, static_cast<std::size_t>(metadata_size), decoded) ||
        decoded.width != 640 || decoded.height != 480 ||
        !decoded.tracks.empty())
        return 5;

    const std::uint32_t video_rtp_timestamp =
        (static_cast<std::uint32_t>(video_packet[4]) << 24U) |
        (static_cast<std::uint32_t>(video_packet[5]) << 16U) |
        (static_cast<std::uint32_t>(video_packet[6]) << 8U) |
        static_cast<std::uint32_t>(video_packet[7]);
    if (decoded.rtp_timestamp != video_rtp_timestamp) return 6;
    return 0;
}
