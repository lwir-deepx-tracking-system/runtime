#include "gui/gui_command_handler.hpp"

#include <stdexcept>

namespace {
GuiCommand ack(const GuiCommand& command, GuiResult result)
{
    return {GuiCategory::System, static_cast<std::uint8_t>(GuiSystem::Ack),
            {static_cast<std::uint8_t>(command.category), command.command,
             static_cast<std::uint8_t>(result)}};
}

// Communication-only implementation: never selects a target or drives a gimbal.
class DeferredCommandHandler final : public IGuiCommandHandler {
public:
    GuiCommand initial_state() const override
    {
        // Manual / None / Idle / no error / target ID 0. No operation is active.
        return {GuiCategory::System, static_cast<std::uint8_t>(GuiSystem::Status),
                std::vector<std::uint8_t>(8, 0)};
    }
    bool handle(const GuiCommand& command, std::vector<GuiCommand>& replies) override
    {
        replies.clear();
        if (command.category == GuiCategory::System) {
            if (command.command != static_cast<std::uint8_t>(GuiSystem::Ping)) return false;
            replies.push_back(ack(command, GuiResult::Ok));
        } else if (command.category == GuiCategory::Mode) {
            if (command.command == static_cast<std::uint8_t>(GuiModeCommand::Manual)) {
                // Idempotent: already Manual with no selected target.
                replies.push_back(ack(command, GuiResult::Ok));
            } else {
                // Preserve the received uint32 ID in GuiCommand, but do not apply it
                // to TargetSelector. Target validation/following is future work.
                bool zero = true;
                for (auto byte : command.payload) zero = zero && byte == 0;
                replies.push_back(ack(command, zero ? GuiResult::InvalidTarget : GuiResult::NotReady));
            }
        } else if (command.category == GuiCategory::Gimbal) {
            // MOVE has no ACK in the PC protocol; it has no effect until a real
            // control strategy exists. HOME must not report fake completion.
            if (command.command == static_cast<std::uint8_t>(GuiGimbalCommand::Home))
                replies.push_back(ack(command, GuiResult::NotReady));
        } else {
            return false;
        }
        return true;
    }
};
}

std::unique_ptr<IGuiCommandHandler> GuiCommandHandlerFactory::create(GuiCommandHandlerType type)
{
    if (type == GuiCommandHandlerType::Deferred) return std::make_unique<DeferredCommandHandler>();
    throw std::runtime_error("Unsupported GUI command handler");
}
