#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "gui/gui_protocol.hpp"

int main()
{
    // 정상 track 선택 명령이 network packet을 거쳐 같은 값으로 복원되는지 확인한다.
    const GuiCommand selected{GuiCommandType::SelectTrack, 7};
    const auto selected_packet = encode_gui_command(selected);
    GuiCommand decoded;
    if (!decode_gui_command(
            selected_packet.data(), selected_packet.size(), decoded) ||
        decoded.type != GuiCommandType::SelectTrack || decoded.track_id != 7)
        return 1;

    // -1은 현재 대상 선택을 해제한다는 의미로 허용한다.
    const GuiCommand cleared{GuiCommandType::SelectTrack, -1};
    const auto cleared_packet = encode_gui_command(cleared);
    if (!decode_gui_command(
            cleared_packet.data(), cleared_packet.size(), decoded) ||
        decoded.track_id != -1)
        return 2;

    // 잘못된 magic이나 허용되지 않은 음수 ID는 수신 측에서 거부해야 한다.
    auto bad_magic = selected_packet;
    bad_magic[0] = 0;
    if (decode_gui_command(bad_magic.data(), bad_magic.size(), decoded))
        return 3;

    const GuiCommand invalid{GuiCommandType::SelectTrack, -2};
    const auto invalid_packet = encode_gui_command(invalid);
    if (decode_gui_command(invalid_packet.data(), invalid_packet.size(), decoded))
        return 4;

    GuiTrackingMetadata metadata;
    metadata.frame_id = 42;
    metadata.rtp_timestamp = 9000;
    metadata.gui_started_us = 1791367200123456ULL;
    metadata.width = 640;
    metadata.height = 480;
    Track track;
    track.track_id = 7;
    track.class_id = 1;
    track.confidence = 0.75F;
    track.x = 10.0F;
    track.y = 20.0F;
    track.width = 30.0F;
    track.height = 40.0F;
    metadata.tracks.push_back(track);

    std::vector<std::uint8_t> metadata_packet;
    if (!encode_tracking_metadata(metadata, 1200, metadata_packet)) return 5;

    GuiTrackingMetadata decoded_metadata;
    if (!decode_tracking_metadata(
            metadata_packet.data(), metadata_packet.size(), decoded_metadata) ||
        decoded_metadata.frame_id != 42 ||
        decoded_metadata.rtp_timestamp != 9000 ||
        decoded_metadata.gui_started_us != 1791367200123456ULL ||
        decoded_metadata.width != 640 ||
        decoded_metadata.height != 480 || decoded_metadata.tracks.size() != 1)
        return 6;
    const Track& decoded_track = decoded_metadata.tracks.front();
    if (decoded_track.track_id != 7 || decoded_track.class_id != 1 ||
        std::fabs(decoded_track.confidence - 0.75F) > 1e-6F ||
        std::fabs(decoded_track.x - 10.0F) > 1e-6F ||
        std::fabs(decoded_track.y - 20.0F) > 1e-6F ||
        std::fabs(decoded_track.width - 30.0F) > 1e-6F ||
        std::fabs(decoded_track.height - 40.0F) > 1e-6F)
        return 7;

    if (encode_tracking_metadata(metadata, 40, metadata_packet)) return 8;

    return 0;
}
