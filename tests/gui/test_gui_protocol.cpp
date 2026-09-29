#include <array>
#include <cstdint>

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

    return 0;
}
