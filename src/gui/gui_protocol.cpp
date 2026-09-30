#include "gui/gui_protocol.hpp"

#include <cstring>
#include <limits>

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

void write_u64(std::uint8_t*& cursor, std::uint64_t value)
{
    write_u32(cursor, static_cast<std::uint32_t>(value >> 32U));
    write_u32(cursor, static_cast<std::uint32_t>(value & 0xffffffffU));
}

void write_i32(std::uint8_t*& cursor, std::int32_t value)
{
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    write_u32(cursor, raw);
}

void write_float(std::uint8_t*& cursor, float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    write_u32(cursor, raw);
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

bool read_u64(
    const std::uint8_t*& cursor,
    const std::uint8_t* end,
    std::uint64_t& value)
{
    std::uint32_t high = 0;
    std::uint32_t low = 0;
    if (!read_u32(cursor, end, high) || !read_u32(cursor, end, low))
        return false;
    value = (static_cast<std::uint64_t>(high) << 32U) | low;
    return true;
}

bool read_i32(
    const std::uint8_t*& cursor,
    const std::uint8_t* end,
    std::int32_t& value)
{
    std::uint32_t raw = 0;
    if (!read_u32(cursor, end, raw)) return false;
    std::memcpy(&value, &raw, sizeof(value));
    return true;
}

bool read_float(
    const std::uint8_t*& cursor,
    const std::uint8_t* end,
    float& value)
{
    std::uint32_t raw = 0;
    if (!read_u32(cursor, end, raw)) return false;
    std::memcpy(&value, &raw, sizeof(value));
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

bool encode_tracking_metadata(
    const GuiTrackingMetadata& metadata,
    std::size_t max_packet_bytes,
    std::vector<std::uint8_t>& packet)
{
    if (metadata.tracks.size() > std::numeric_limits<std::uint32_t>::max())
        return false;
    if (metadata.tracks.size() >
        (std::numeric_limits<std::size_t>::max() - kGuiMetadataHeaderSize) /
            kGuiTrackMetadataSize)
        return false;

    const std::size_t packet_size = kGuiMetadataHeaderSize +
        metadata.tracks.size() * kGuiTrackMetadataSize;
    if (packet_size > max_packet_bytes) return false;

    packet.resize(packet_size);
    std::uint8_t* cursor = packet.data();
    write_u32(cursor, kGuiMetadataMagic);
    write_u16(cursor, kGuiProtocolVersion);
    write_u16(cursor, static_cast<std::uint16_t>(GuiMetadataType::Tracking));
    write_u64(cursor, metadata.frame_id);
    write_u32(cursor, metadata.rtp_timestamp);
    write_u32(cursor, metadata.width);
    write_u32(cursor, metadata.height);
    write_u32(cursor, static_cast<std::uint32_t>(metadata.tracks.size()));

    for (const Track& track : metadata.tracks)
    {
        write_i32(cursor, track.track_id);
        write_i32(cursor, track.class_id);
        write_float(cursor, track.confidence);
        write_float(cursor, track.x);
        write_float(cursor, track.y);
        write_float(cursor, track.width);
        write_float(cursor, track.height);
    }
    return cursor == packet.data() + packet.size();
}

bool decode_tracking_metadata(
    const std::uint8_t* data,
    std::size_t size,
    GuiTrackingMetadata& metadata)
{
    if (!data || size < kGuiMetadataHeaderSize) return false;

    const std::uint8_t* cursor = data;
    const std::uint8_t* end = data + size;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t raw_type = 0;
    std::uint32_t track_count = 0;
    if (!read_u32(cursor, end, magic) ||
        !read_u16(cursor, end, version) ||
        !read_u16(cursor, end, raw_type) ||
        !read_u64(cursor, end, metadata.frame_id) ||
        !read_u32(cursor, end, metadata.rtp_timestamp) ||
        !read_u32(cursor, end, metadata.width) ||
        !read_u32(cursor, end, metadata.height) ||
        !read_u32(cursor, end, track_count))
        return false;

    if (magic != kGuiMetadataMagic || version != kGuiProtocolVersion ||
        raw_type != static_cast<std::uint16_t>(GuiMetadataType::Tracking))
        return false;
    if (static_cast<std::size_t>(track_count) >
        (std::numeric_limits<std::size_t>::max() - kGuiMetadataHeaderSize) /
            kGuiTrackMetadataSize)
        return false;
    if (size != kGuiMetadataHeaderSize +
            static_cast<std::size_t>(track_count) * kGuiTrackMetadataSize)
        return false;

    metadata.tracks.clear();
    metadata.tracks.reserve(track_count);
    for (std::uint32_t i = 0; i < track_count; ++i)
    {
        Track track;
        std::int32_t track_id = -1;
        std::int32_t class_id = -1;
        if (!read_i32(cursor, end, track_id) ||
            !read_i32(cursor, end, class_id) ||
            !read_float(cursor, end, track.confidence) ||
            !read_float(cursor, end, track.x) ||
            !read_float(cursor, end, track.y) ||
            !read_float(cursor, end, track.width) ||
            !read_float(cursor, end, track.height))
            return false;
        track.track_id = track_id;
        track.class_id = class_id;
        metadata.tracks.push_back(track);
    }
    return cursor == end;
}
