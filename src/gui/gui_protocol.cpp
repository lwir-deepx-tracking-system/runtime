#include "gui/gui_protocol.hpp"

#include <cstring>

#include <arpa/inet.h>

namespace {
// 정수 값을 packet의 현재 위치에 network byte order로 기록한다.
void write_u16(std::uint8_t*& cursor, std::uint16_t value)
{
    value = htons(value);
    std::memcpy(cursor, &value, sizeof(value));
    cursor += sizeof(value);
}

void write_u32(std::uint8_t*& cursor, std::uint32_t value)
{
    value = htonl(value);
    std::memcpy(cursor, &value, sizeof(value));
    cursor += sizeof(value);
}

bool read_u16(
    const std::uint8_t*& cursor,
    const std::uint8_t* end,
    std::uint16_t& value)
{
    if (static_cast<std::size_t>(end - cursor) < sizeof(value)) return false;
    std::memcpy(&value, cursor, sizeof(value));
    cursor += sizeof(value);
    value = ntohs(value);
    return true;
}

bool read_u32(
    const std::uint8_t*& cursor,
    const std::uint8_t* end,
    std::uint32_t& value)
{
    if (static_cast<std::size_t>(end - cursor) < sizeof(value)) return false;
    std::memcpy(&value, cursor, sizeof(value));
    cursor += sizeof(value);
    value = ntohl(value);
    return true;
}
}

std::array<std::uint8_t, kGuiCommandPacketSize> encode_gui_command(
    const GuiCommand& command)
{
    std::array<std::uint8_t, kGuiCommandPacketSize> packet{};
    std::uint8_t* cursor = packet.data();
    write_u32(cursor, kGuiCommandMagic);
    write_u16(cursor, kGuiProtocolVersion);
    write_u16(cursor, static_cast<std::uint16_t>(command.type));

    // int의 메모리 표현을 직접 전송하지 않고 명시적인 32-bit 값으로 바꾼다.
    // -1의 bit pattern도 그대로 보존하기 위해 memcpy로 signed/unsigned를 옮긴다.
    const std::int32_t track_id = static_cast<std::int32_t>(command.track_id);
    std::uint32_t raw_track_id = 0;
    std::memcpy(&raw_track_id, &track_id, sizeof(raw_track_id));
    write_u32(cursor, raw_track_id);
    return packet;
}

bool decode_gui_command(
    const std::uint8_t* data,
    std::size_t size,
    GuiCommand& command)
{
    if (!data || size != kGuiCommandPacketSize) return false;

    const std::uint8_t* cursor = data;
    const std::uint8_t* end = data + size;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t raw_type = 0;
    std::uint32_t raw_track_id = 0;
    if (!read_u32(cursor, end, magic) ||
        !read_u16(cursor, end, version) ||
        !read_u16(cursor, end, raw_type) ||
        !read_u32(cursor, end, raw_track_id))
        return false;

    if (magic != kGuiCommandMagic || version != kGuiProtocolVersion ||
        raw_type != static_cast<std::uint16_t>(GuiCommandType::SelectTrack))
        return false;

    std::int32_t track_id = 0;
    std::memcpy(&track_id, &raw_track_id, sizeof(track_id));
    if (track_id < -1) return false;

    command.type = GuiCommandType::SelectTrack;
    command.track_id = track_id;
    return cursor == end;
}
