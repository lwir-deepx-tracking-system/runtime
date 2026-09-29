#include <cstdint>
#include <memory>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <opencv2/core.hpp>

#include "common/frame.hpp"
#include "common/track.hpp"
#include "gui/gui_config.hpp"
#include "gui/gui_sender.hpp"

int main()
{
    // 임시 loopback UDP 포트를 열어 udpsink가 만든 RTP packet까지 확인한다.
    const int receiver_fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receiver_fd < 0) return 1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(
            receiver_fd, reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) != 0)
    {
        ::close(receiver_fd);
        return 2;
    }
    socklen_t address_size = sizeof(address);
    if (::getsockname(
            receiver_fd, reinterpret_cast<sockaddr*>(&address),
            &address_size) != 0)
    {
        ::close(receiver_fd);
        return 3;
    }

    GuiVideoConfig config;
    config.host = "127.0.0.1";
    config.port = ntohs(address.sin_port);
    config.encoder = "x264enc";
    config.bitrate_kbps = 1000;
    config.fps = 30;
    config.rtp_mtu = 1200;

    GuiSender sender(config, 8000, 24000);

    // 실제 inference 결과를 만들지 않고, 표시 경계가 요구하는 CV_16UC1
    // frame과 빈 Track 목록만 사용해 GStreamer 송신 경로를 검증한다.
    auto frame = std::make_shared<FrameContext>();
    frame->image = cv::Mat(480, 640, CV_16UC1, cv::Scalar(16000));

    TrackingResult result;
    result.frame = frame;
    for (std::uint64_t frame_id = 0; frame_id < 3; ++frame_id)
    {
        frame->metadata.frame_id = frame_id;
        if (!sender.send(result))
        {
            ::close(receiver_fd);
            return 4;
        }
    }

    pollfd descriptor{};
    descriptor.fd = receiver_fd;
    descriptor.events = POLLIN;
    if (::poll(&descriptor, 1, 2000) <= 0)
    {
        sender.stop();
        ::close(receiver_fd);
        return 5;
    }

    std::uint8_t packet[2048]{};
    const ssize_t received = ::recv(receiver_fd, packet, sizeof(packet), 0);
    sender.stop();
    ::close(receiver_fd);

    // RTP v2 header와 설정한 dynamic payload type 96을 확인한다.
    if (received < 12 || (packet[0] >> 6U) != 2U ||
        (packet[1] & 0x7fU) != 96U)
        return 6;
    return 0;
}
