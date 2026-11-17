#include <future>
#include <stdexcept>
#include <string>
#include "gui/gui_command_handler.hpp"
#include "app/pipeline_requests.hpp"

namespace {
std::vector<std::uint8_t> hex(const std::string& s) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
    return out;
}
// 추론 없이 핸들러 연관 호출과 비동기 응답 계약을 검증한다.
class Requests final : public IPipelineRequests {
public:
    std::promise<GuiPipelineResult> model, tracker, policy;
    TrackerPolicyUpdate policy_update;
    std::uint32_t model_id = 0, tracker_id = 0;
    PipelineRecordingStatus rec{};
    bool rec_allowed = true;
    std::string rec_name;
    std::uint32_t model_count() const override { return 2; }
    std::uint32_t tracker_count() const override { return 1; }
    PipelineCatalogItem model_info(std::uint32_t id) const override {
        return id == 2 ? PipelineCatalogItem{id, "YOLO26s", true, true} : PipelineCatalogItem{};
    }
    PipelineCatalogItem tracker_info(std::uint32_t id) const override {
        return id == 1 ? PipelineCatalogItem{id, "ByteTrack", true, true} : PipelineCatalogItem{};
    }
    PipelineTrackerPolicy tracker_policy() const override {
        return {1, 45000, 0, 0.5F, 3, 130, 7, 2, 1, true, 0.5F};
    }
    PipelineAutoDetection auto_detection() const override { return {false,true,12,13,100,140}; }
    bool set_auto_detection(bool enabled) override { auto_enabled=enabled;return true; }
    bool set_auto_detection_thresholds(float far,float near) override {
        auto_far=far;auto_near=near;return far<near;
    }
    bool auto_enabled=false;
    float auto_far=0,auto_near=0;
    PipelineRecordingStatus recording_status() const override { return rec; }
    bool recording_available() const override { return rec_allowed; }
    bool start_recording(const std::string& name) override {
        rec_name = name;
        rec.active = true;
        rec.session = 1;
        return true;
    }
    bool stop_recording() override { rec.active = false; rec.frames = 3; return true; }
    std::string latest_recording() const override { return "MEASURE_20261116_142347_0"; }
    std::vector<std::string> list_recordings() const override { return {latest_recording()}; }
    PipelineRecordingFileChunk read_recording_file(
        const std::string& name, std::uint8_t file, std::uint64_t offset,
        std::uint16_t limit) const override {
        if (name != latest_recording() || file != 1 || offset != 0 || limit != 4) return {};
        return {true, 2, {'O', 'K'}};
    }
    PipelineChangeTicket request_model_change(std::uint32_t id) override {
        model_id = id;
        if (id < 1 || id > 2) return {GuiPipelineResult::InvalidId, {}};
        return {GuiPipelineResult::Ok, model.get_future().share()};
    }
    PipelineChangeTicket request_tracker_change(std::uint32_t id) override {
        tracker_id = id;
        if (id != 1) return {GuiPipelineResult::InvalidId, {}};
        return {GuiPipelineResult::Ok, tracker.get_future().share()};
    }
    PipelineChangeTicket request_policy_change(const TrackerPolicyUpdate& update) override {
        policy_update = update;
        return {GuiPipelineResult::Ok, policy.get_future().share()};
    }
};
}

int main() {
    Requests requests;
    auto handler = GuiCommandHandlerFactory::create(GuiCommandHandlerType::Deferred, {}, nullptr, &requests);
    std::vector<GuiCommand> replies;
    const auto handle = [&](const std::string& input, const std::string& expected) {
        auto packet = hex(input);
        GuiCommand command;
        if (!decode_gui_command(packet.data(), packet.size(), command) ||
            !handler->handle(command, replies) || replies.size() != 1) return false;
        return encode_gui_command(replies[0]) == hex(expected);
    };
    if (!handle("4c5749520401000412345678", "4c574952048100081234567800000002")) return 1;
    if (!handle("4c5749520402000487654321", "4c574952048200088765432100000001")) return 2;
    if (!handle("4c5749520407000400000005",
                "4c5749520487002000000005000000010000afc8000000003f000000000000030000008200000007")) return 19;
    if (!handle("4c5749520409000400000006",
                "4c57495204890014000000060000000200000001000000013f000000"))return 32;
    if (!handle("4c574952040b000400000007",
                "4c574952048b001c0000000700000000000000010000000c0000000d42c80000430c0000"))return 33;
    if (!handle("4c574952040c00080000000800000001",
                "4c574952048c0006000000080200") || !requests.auto_enabled)return 34;
    if (!handle("4c574952040d000c0000000942a0000042f00000",
                "4c574952048d0006000000090200") ||
        requests.auto_far!=80 || requests.auto_near!=120)return 37;
    {
        GuiCommand start{GuiCategory::Recording, 2, std::vector<std::uint8_t>(69, 0)};
        start.payload[3] = 11;
        start.payload[4] = 1;
        start.payload[5] = 'A';
        const auto packet = encode_gui_command(start);
        GuiCommand parsed;
        if (!decode_gui_command(packet.data(), packet.size(), parsed) ||
            !handler->handle(parsed, replies) || replies.size() != 1 ||
            replies[0].command != 0x82 || replies[0].payload.size() != 28 ||
            replies[0].payload[5] != 1 || requests.rec_name != "A") return 24;
        GuiCommand stop{GuiCategory::Recording, 3, hex("0000000c")};
        if (!handler->handle(stop, replies) || replies[0].command != 0x83 ||
            replies[0].payload[5] != 0 || replies[0].payload[19] != 3) return 25;
        GuiCommand latest{GuiCategory::Recording, 4, hex("0000000d")};
        const auto rec_name = requests.latest_recording();
        if (!handler->handle(latest, replies) || replies[0].command != 0x84 ||
            replies[0].payload.size() != 69 || replies[0].payload[4] != 0 ||
            std::string(replies[0].payload.begin() + 5,
                        replies[0].payload.begin() + 5 + rec_name.size()) != rec_name) return 26;
        GuiCommand list{GuiCategory::Recording, 6, hex("0000000f")};
        if (!handler->handle(list, replies) || replies[0].command != 0x86 ||
            replies[0].payload.size() != 69 || replies[0].payload[4] != 0 ||
            std::string(replies[0].payload.begin() + 5,
                        replies[0].payload.begin() + 5 + rec_name.size()) != rec_name ||
            encode_gui_command(replies[0]).empty()) return 28;
        GuiCommand read{GuiCategory::Recording, 5, std::vector<std::uint8_t>(79, 0)};
        read.payload[3] = 14;
        std::copy(rec_name.begin(), rec_name.end(), read.payload.begin() + 4);
        read.payload[68] = 1;
        read.payload[78] = 4;
        auto wire = encode_gui_command(read);
        GuiCommand decoded;
        if (!decode_gui_command(wire.data(), wire.size(), decoded) ||
            !handler->handle(decoded, replies) || replies[0].command != 0x85 ||
            replies[0].payload.size() != 23 || replies[0].payload[4] != 0 ||
            replies[0].payload[12] != 2 || replies[0].payload[21] != 'O' ||
            replies[0].payload[22] != 'K') return 27;
        wire = encode_gui_command(replies[0]);
        if (!decode_gui_command(wire.data(), wire.size(), decoded)) return 28;
        requests.rec_allowed = false;
        if (!handler->handle(list, replies) || replies[0].payload.size() != 5 ||
            replies[0].payload[4] != 1) return 29;
        if (!handler->handle(start, replies) || replies[0].payload[4] != 1) return 30;
        if (!handler->handle(read, replies) || replies[0].payload[4] != 1) return 31;
        requests.rec_allowed = true;
    }
    {
        const auto packet = hex("4c574952040500080000000300000002");
        GuiCommand request;
        if (!decode_gui_command(packet.data(), packet.size(), request) ||
            !handler->handle(request, replies) || replies.size() != 1) return 15;
        const auto& info = replies[0];
        if (info.command != 0x85 || info.payload.size() != 76 ||
            info.payload[3] != 3 || info.payload[7] != 2 || info.payload[11] != 3 ||
            std::string(info.payload.begin() + 12, info.payload.begin() + 19) != "YOLO26s") return 16;
    }
    {
        const auto packet = hex("4c574952040600080000000400000001");
        GuiCommand request;
        if (!decode_gui_command(packet.data(), packet.size(), request) ||
            !handler->handle(request, replies) || replies.size() != 1) return 17;
        if (replies[0].command != 0x86 || replies[0].payload.size() != 76 ||
            std::string(replies[0].payload.begin() + 12, replies[0].payload.begin() + 21) != "ByteTrack") return 18;
    }
    if (!handle("4c57495204030008ffffffff00000002", "4c57495204830006ffffffff0100") || requests.model_id != 2) return 3;
    // 전환 완료 전에도 PING에 응답하고 대기 중인 요청 ID의 중복은 거부한다.
    if (!handle("4c57495201040000", "4c57495201010003010400")) return 4;
    if (!handle("4c57495204040008ffffffff00000001", "4c57495204840006ffffffff0202")) return 5;
    if (!handle("4c574952040400080000000100000001", "4c57495204840006000000010100") || requests.tracker_id != 1) return 6;
    handler->tick(replies);
    if (!replies.empty()) return 7;
    requests.model.set_value(GuiPipelineResult::Restored);
    requests.tracker.set_value(GuiPipelineResult::CreationFailed);
    handler->tick(replies);
    if (replies.size() != 2 || encode_gui_command(replies[0]) != hex("4c57495204830006ffffffff0204") ||
        encode_gui_command(replies[1]) != hex("4c57495204840006000000010203")) return 8;
    if (!handle("4c5749520408001c000000090000000f00001f400000fa003f0000000000008200000000",
                "4c57495204880006000000090100")) return 20;
    if (!requests.policy_update.warmth_min || *requests.policy_update.warmth_min != 8000 ||
        !requests.policy_update.warmth_max || *requests.policy_update.warmth_max != 64000 ||
        !requests.policy_update.edge_birth || *requests.policy_update.edge_birth != 0.5F ||
        !requests.policy_update.confirmation_ms || *requests.policy_update.confirmation_ms != 130)
        return 21;
    requests.policy.set_value(GuiPipelineResult::Ok);
    handler->tick(replies);
    if (replies.size() != 1 || encode_gui_command(replies[0]) !=
        hex("4c57495204880006000000090200")) return 22;
    if (!handle("4c5749520408001c0000000a000000000000000000000000000000000000000000000000",
                "4c574952048800060000000a0207")) return 23;
    if (!handle("4c5749520403000800000002ffffffff", "4c57495204830006000000020201") || requests.model_id != 0xffffffffU) return 9;
    GuiCommand board_reply{GuiCategory::Pipeline, 0x81, std::vector<std::uint8_t>(8)};
    if (handler->handle(board_reply, replies)) return 10;
    auto malformed = hex("4c5749520403ffff");
    PcGuiProtocol protocol;
    if (protocol.command_packet_size(malformed.data(), malformed.size())) return 11;

    Requests feature_requests;
    auto feature_handler=GuiCommandHandlerFactory::create(
        GuiCommandHandlerType::Deferred,{},nullptr,&feature_requests);
    GuiCommand feature_command{GuiCategory::Pipeline,10,
        hex("000000150000000f0000000100000002000000013f000000")};
    if(!feature_handler->handle(feature_command,replies) || replies.size()!=1 ||
       replies[0].command!=0x8a || replies[0].payload.back()!=0 ||
       !feature_requests.policy_update.position_memory ||
       *feature_requests.policy_update.position_memory!="gru_kpt" ||
       *feature_requests.policy_update.box_refine_mode!="body" ||
       !*feature_requests.policy_update.box_refine_lost_same ||
       *feature_requests.policy_update.box_refine_aim_y!=0.5)return 35;
    feature_requests.policy.set_value(GuiPipelineResult::Ok);
    feature_handler->tick(replies);
    if(replies.size()!=1 || replies[0].command!=0x8a || replies[0].payload[4]!=2)return 36;

    Requests disconnected;
    handler = GuiCommandHandlerFactory::create(GuiCommandHandlerType::Deferred, {}, nullptr, &disconnected);
    if (!handle("4c574952040300080000000100000002", "4c57495204830006000000010100")) return 12;
    handler->disconnected();
    handler->connected();
    disconnected.model.set_value(GuiPipelineResult::Ok);
    handler->tick(replies);
    if (!replies.empty()) return 13;
    auto unavailable = GuiCommandHandlerFactory::create(GuiCommandHandlerType::Deferred);
    GuiCommand change{GuiCategory::Pipeline, 3, hex("0000000100000001")};
    if (!unavailable->handle(change, replies) || replies.size() != 1 || replies[0].payload.back() != 6) return 14;
    return 0;
}
