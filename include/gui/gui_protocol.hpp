#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/track.hpp"

// GUI와 runtime 사이의 C++ 구조체를 그대로 socket으로 보내지 않고 명시적인
// byte layout으로 encode/decode한다.
//
// 이유:
// - CPU endian 차이를 network byte order로 통일
// - compiler의 struct padding과 ABI 차이에 의존하지 않음
// - magic, version, type으로 잘못된 packet을 경계에서 거부
//
// Command(TCP):
//   magic + version + type + track_id
// Tracking metadata(UDP):
//   magic + version + type + frame_id + RTP timestamp + GUI start timestamp
//   + frame size
//   + track count + 반복되는 Track ID/class/confidence/bbox
//
// TCP는 message 경계를 보존하지 않으므로 command packet 크기를 고정하고,
// UDP metadata는 datagram 하나가 한 frame의 metadata 전체를 담는다.
constexpr std::uint32_t kGuiCommandMagic = 0x4c574952U; // ASCII "LWIR"
constexpr std::uint16_t kGuiProtocolVersion = 2U;
constexpr std::size_t kGuiCommandPacketSize = 12U;

constexpr std::uint32_t kGuiMetadataMagic = 0x4c57494dU; // ASCII "LWIM"
constexpr std::size_t kGuiMetadataHeaderSize = 40U;
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

// 영상과 별도 UDP 채널로 전송되는 한 frame의 추적 정보이다. GUI는
// frame_id/rtp_timestamp로 영상과 대응시키며 bbox는 letterbox 좌표가 아니라
// GUI 영상과 동일한 원본 frame pixel 좌표이다.
struct GuiTrackingMetadata
{
    std::uint64_t frame_id = 0;
    std::uint32_t rtp_timestamp = 0;
    // GuiSenderThread가 송신 처리를 시작한 system_clock epoch microsecond이다.
    std::uint64_t gui_started_us = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<Track> tracks;
};

// GuiCommand를 고정 12-byte command packet으로 직렬화한다.
std::array<std::uint8_t, kGuiCommandPacketSize> encode_gui_command(
    const GuiCommand& command);

// command packet의 크기와 header를 검증한 뒤 선택 track_id를 복원한다.
bool decode_gui_command(
    const std::uint8_t* data,
    std::size_t size,
    GuiCommand& command);

// 한 frame의 header와 모든 Track을 metadata UDP datagram 하나로 직렬화한다.
// max_packet_bytes를 넘으면 IP fragmentation을 피하기 위해 전체를 실패시키며,
// 일부 Track만 조용히 버리지는 않는다.
bool encode_tracking_metadata(
    const GuiTrackingMetadata& metadata,
    std::size_t max_packet_bytes,
    std::vector<std::uint8_t>& packet);

// metadata header와 전체 packet 크기를 검증한 뒤 원본 좌표 Track 목록을 복원한다.
bool decode_tracking_metadata(
    const std::uint8_t* data,
    std::size_t size,
    GuiTrackingMetadata& metadata);
