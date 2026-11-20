#include "gui/gui_metadata_sender.hpp"
#include "common/frame_telemetry_collector.hpp"
#include "common/logger.hpp"
#include <cstring>
#include <netdb.h>
#include <unistd.h>

// 설정된 PC GUI 주소로 metadata를 보낼 UDP socket과 목적지 주소를 준비한다.
bool GuiMetadataSender::open_metadata_socket()
{
    if (!metadata_config_.enabled || metadata_fd_ >= 0) return true;

    // metadata는 JPEG or H.264/RTP pipeline과 독립된 UDP datagram 채널이다.
    // getaddrinfo를 사용해 IPv4/IPv6 어느 주소든 설정값으로 지정할 수 있게 한다.
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* addresses = nullptr;
    const std::string port = std::to_string(metadata_config_.port);
    const int lookup = ::getaddrinfo(
        metadata_config_.host.c_str(), port.c_str(), &hints, &addresses);
    if (lookup != 0)
    {
        Logger::error(
            "[GuiSender] metadata 주소 해석 실패: " +
            std::string(gai_strerror(lookup)));
        return false;
    }

    for (addrinfo* address = addresses; address; address = address->ai_next)
    {
        if (address->ai_addrlen > sizeof(metadata_address_)) continue;
        const int fd = ::socket(
            address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;

        metadata_fd_ = fd;
        std::memcpy(
            &metadata_address_, address->ai_addr, address->ai_addrlen);
        metadata_address_size_ = address->ai_addrlen;
        break;
    }
    ::freeaddrinfo(addresses);

    if (metadata_fd_ < 0)
    {
        Logger::error("[GuiSender] metadata UDP socket 생성 실패");
        return false;
    }
    Logger::info(
        "[GuiSender] bbox metadata UDP 송신 시작: " +
        metadata_config_.host + ":" + port);
    return true;
}

// 현재 TrackingResult를 영상 frame과 대응 가능한 metadata datagram으로 보낸다.
bool GuiMetadataSender::send(
    const TrackingResult& result,
    std::uint64_t gui_started_us, std::uint32_t timestamp,
    FrameTelemetryCollector* telemetry)
{
    if (!metadata_config_.enabled) return true;
    if (!open_metadata_socket()) return false;

    // GUI는 frame_id와 RTP timestamp로 metadata를 영상 frame에 대응시키고,
    // 원본 frame 크기와 bbox를 사용해 영상 위에 Track을 표시한다.
    GuiTrackingMetadata metadata;
    metadata.frame_id = result.frame->metadata.frame_id;
    // metadata와 RTP 영상을 같은 frame으로 대응시키기 위한 timestamp이다.
    // GStreamer H.264 RTP clock의 90kHz 기준으로 현재 frame index를 변환한다.
    metadata.rtp_timestamp = timestamp;
    metadata.gui_started_us = gui_started_us;
    metadata.telemetry_enabled = metadata_config_.telemetry_enabled;
    if (telemetry)
        metadata.telemetry = telemetry->latest_before(metadata.frame_id,
            result.model_generation, result.tracker_epoch);
    metadata.width = static_cast<std::uint32_t>(result.frame->image.cols);
    metadata.height = static_cast<std::uint32_t>(result.frame->image.rows);
    metadata.tracks.reserve(result.tracks.size());
    for (const auto& source : result.tracks) {
        // Adapt internal int/-1 IDs and class sentinel at the communication
        // boundary. Do not cast invalid signed IDs into valid uint32 IDs.
        if (source.track_id <= 0 || source.class_id < -1 || source.class_id > 255) continue;
        GuiTrackMetadata track;
        track.track_id = static_cast<std::uint32_t>(source.track_id);
        track.x = source.x; track.y = source.y;
        track.width = source.width; track.height = source.height;
        track.confidence = source.confidence;
        track.class_id = source.class_id < 0 ? 255 : static_cast<std::uint8_t>(source.class_id);
        metadata.tracks.push_back(track);
    }

    // C++ Track 구조체를 직접 보내지 않고 protocol의 고정 byte layout으로 만든다.
    std::vector<std::uint8_t> packet;
    if (!protocol_.encode_metadata(
            metadata, metadata_config_.max_packet_bytes, packet))
    {
        Logger::error(
            "[GuiSender] Metadata is not representable in the configured protocol/packet limit");
        return false;
    }

    const ssize_t sent = ::sendto(
        metadata_fd_, packet.data(), packet.size(), 0,
        reinterpret_cast<const sockaddr*>(&metadata_address_),
        metadata_address_size_);
    return sent == static_cast<ssize_t>(packet.size());
}

GuiMetadataSender::~GuiMetadataSender() { close(); }

void GuiMetadataSender::close()
{
    if (metadata_fd_ >= 0)
    {
        ::close(metadata_fd_);
        metadata_fd_ = -1;
        metadata_address_size_ = 0;
    }
}
