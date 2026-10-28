#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <vector>

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

std::uint32_t read_rtp_timestamp(const std::uint8_t* packet)
{
    return (static_cast<std::uint32_t>(packet[4]) << 24U) |
        (static_cast<std::uint32_t>(packet[5]) << 16U) |
        (static_cast<std::uint32_t>(packet[6]) << 8U) |
        static_cast<std::uint32_t>(packet[7]);
}

bool receive_matching_packets(
    int video_fd,
    int metadata_fd,
    std::uint64_t gui_started_us,
    GuiTrackingMetadata& matched_metadata)
{
    std::set<std::uint32_t> video_timestamps;
    std::vector<GuiTrackingMetadata> metadata_packets;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() < deadline)
    {
        pollfd descriptors[2]{};
        descriptors[0].fd = video_fd;
        descriptors[0].events = POLLIN;
        descriptors[1].fd = metadata_fd;
        descriptors[1].events = POLLIN;
        if (::poll(descriptors, 2, 500) <= 0)
            continue;

        if (descriptors[0].revents & POLLIN)
        {
            std::uint8_t packet[2048]{};
            const ssize_t size = ::recv(video_fd, packet, sizeof(packet), 0);
            if (size >= 12 && (packet[0] >> 6U) == 2U &&
                (packet[1] & 0x7fU) == 96U)
                video_timestamps.insert(read_rtp_timestamp(packet));
        }

        if (descriptors[1].revents & POLLIN)
        {
            std::uint8_t packet[2048]{};
            const ssize_t size = ::recv(metadata_fd, packet, sizeof(packet), 0);
            GuiTrackingMetadata decoded;
            if (size > 0 && decode_tracking_metadata(
                    packet, static_cast<std::size_t>(size), decoded))
                metadata_packets.push_back(decoded);
        }

        for (const GuiTrackingMetadata& metadata : metadata_packets)
        {
            if (metadata.width == 640 && metadata.height == 480 &&
                metadata.gui_started_us == gui_started_us &&
                metadata.tracks.empty() &&
                video_timestamps.count(metadata.rtp_timestamp) != 0)
            {
                matched_metadata = metadata;
                return true;
            }
        }
    }
    std::fprintf(stderr, "received video timestamps=%zu metadata packets=%zu\n",
        video_timestamps.size(), metadata_packets.size());
    for (const std::uint32_t timestamp : video_timestamps)
        std::fprintf(stderr, "video RTP timestamp=%u\n", timestamp);
    for (const GuiTrackingMetadata& metadata : metadata_packets)
        std::fprintf(stderr, "metadata frame=%llu timestamp=%u\n",
            static_cast<unsigned long long>(metadata.frame_id),
            metadata.rtp_timestamp);
    return false;
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
    constexpr std::uint64_t kGuiStartedUs = 1791367200123456ULL;
    for (std::uint64_t frame_id = 0; frame_id < 3; ++frame_id)
    {
        frame->metadata.frame_id = frame_id;
        if (!sender.send(result, kGuiStartedUs))
        {
            sender.stop();
            ::close(video_fd);
            ::close(metadata_fd);
            return 2;
        }
    }

    GuiTrackingMetadata decoded;
    if (!receive_matching_packets(
            video_fd, metadata_fd, kGuiStartedUs, decoded))
    {
        sender.stop();
        ::close(video_fd);
        ::close(metadata_fd);
        return 3;
    }

    sender.stop();
    ::close(video_fd);
    ::close(metadata_fd);
    return 0;
}
