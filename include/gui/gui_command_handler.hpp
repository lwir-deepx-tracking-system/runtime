#pragma once

#include "config/app_config.hpp"
#include "gui/gui_protocol.hpp"

// Application policy strategy. It has no socket, YAML or tracking dependencies.
// Future target-following implementations replace this strategy via the factory.
class IGuiCommandHandler {
public:
    virtual ~IGuiCommandHandler() = default;
    virtual GuiCommand initial_state() const = 0;
    // Input must first pass the configured wire codec validation.
    // false: invalid direction (e.g. a PC sending board-only STATUS); disconnect.
    virtual bool handle(const GuiCommand&, std::vector<GuiCommand>& replies) = 0;
};

class GuiCommandHandlerFactory {
public:
    static std::unique_ptr<IGuiCommandHandler> create(GuiCommandHandlerType type);
};
