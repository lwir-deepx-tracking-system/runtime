#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/track.hpp"

// TCP는 message 경계를 보존하지 않으므로 명령 packet 크기를 고정한다.
// wire layout: magic(4) + version(2) + type(2) + track_id(4)
constexpr std::uint32_t kGuiCommandMagic = 0x4c574952U; // ASCII "LWIR"
constexpr std::uint16_t kGuiProtocolVersion = 1U;
constexpr std::size_t kGuiCommandPacketSize = 12U;

constexpr std::uint32_t kGuiMetadataMagic = 0x4c57494dU; // ASCII "LWIM"
constexpr std::size_t kGuiMetadataHeaderSize = 32U;
constexpr std::size_t kGuiTrackMetadataSize = 28U;

enum class GuiCommandType : std::uint16_t
{
    SelectTrack = 1
};

enum class GuiMetadataType : std::uint16_t
{
    Tracking = 1
};

struct GuiCommand
{
    GuiCommandType type = GuiCommandType::SelectTrack;

    // -1은 선택 해제, 0 이상은 Tracker가 부여한 ID이다.
    int track_id = -1;
};

// 영상과 별도로 전송되는 한 frame의 추적 정보이다. bbox는 모델 letterbox
// 좌표가 아니라 GUI 영상과 동일한 원본 frame pixel 좌표이다.
struct GuiTrackingMetadata
{
    std::uint64_t frame_id = 0;
    std::uint32_t rtp_timestamp = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<Track> tracks;
};

// C++ 구조체의 padding이나 CPU endian에 의존하지 않도록 명시적으로
// network byte order로 직렬화한다.
std::array<std::uint8_t, kGuiCommandPacketSize> encode_gui_command(
    const GuiCommand& command);

bool decode_gui_command(
    const std::uint8_t* data,
    std::size_t size,
    GuiCommand& command);

// UDP datagram 하나에 metadata 한 frame을 넣는다. max_packet_bytes를 넘으면
// IP fragmentation을 피하기 위해 실패하며 일부 Track만 조용히 버리지 않는다.
bool encode_tracking_metadata(
    const GuiTrackingMetadata& metadata,
    std::size_t max_packet_bytes,
    std::vector<std::uint8_t>& packet);

bool decode_tracking_metadata(
    const std::uint8_t* data,
    std::size_t size,
    GuiTrackingMetadata& metadata);
