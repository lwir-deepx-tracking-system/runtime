#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// TCP는 message 경계를 보존하지 않으므로 명령 packet 크기를 고정한다.
// wire layout: magic(4) + version(2) + type(2) + track_id(4)
constexpr std::uint32_t kGuiCommandMagic = 0x4c574952U; // ASCII "LWIR"
constexpr std::uint16_t kGuiProtocolVersion = 1U;
constexpr std::size_t kGuiCommandPacketSize = 12U;

enum class GuiCommandType : std::uint16_t
{
    SelectTrack = 1
};

struct GuiCommand
{
    GuiCommandType type = GuiCommandType::SelectTrack;

    // -1은 선택 해제, 0 이상은 Tracker가 부여한 ID이다.
    int track_id = -1;
};

// C++ 구조체의 padding이나 CPU endian에 의존하지 않도록 명시적으로
// network byte order로 직렬화한다.
std::array<std::uint8_t, kGuiCommandPacketSize> encode_gui_command(
    const GuiCommand& command);

bool decode_gui_command(
    const std::uint8_t* data,
    std::size_t size,
    GuiCommand& command);
