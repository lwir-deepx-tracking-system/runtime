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
    std::uint32_t model_count() const override { return 2; }
    std::uint32_t tracker_count() const override { return 1; }
    PipelineCatalogItem model_info(std::uint32_t id) const override {
        return id == 2 ? PipelineCatalogItem{id, "YOLO26s", true, true} : PipelineCatalogItem{};
    }
    PipelineCatalogItem tracker_info(std::uint32_t id) const override {
        return id == 1 ? PipelineCatalogItem{id, "ByteTrack", true, true} : PipelineCatalogItem{};
    }
    PipelineTrackerPolicy tracker_policy() const override {
        return {1, 45000, 0, 0.5F, 3, 130, 7};
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
