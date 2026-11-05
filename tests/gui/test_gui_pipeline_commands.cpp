#include <future>
#include <stdexcept>
#include <string>
#include "gui/gui_command_handler.hpp"
#include "gui/pipeline_requests.hpp"

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
    std::promise<GuiPipelineResult> model, tracker;
    std::uint32_t model_id = 0, tracker_id = 0;
    std::uint32_t model_count() const override { return 2; }
    std::uint32_t tracker_count() const override { return 1; }
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
