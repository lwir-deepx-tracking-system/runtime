#include "control/manual_command_handler.hpp"

#include <stdexcept>
#include <utility>
#include "common/logger.hpp"

namespace {
GuiCommand ack(const GuiCommand& request, GuiResult result)
{
    return {GuiCategory::System, static_cast<std::uint8_t>(GuiSystem::Ack),
            {static_cast<std::uint8_t>(request.category), request.command,
             static_cast<std::uint8_t>(result)}};
}
int direction(std::uint8_t byte) { return byte < 128 ? byte : static_cast<int>(byte) - 256; }
}
ManualCommandHandler::ManualCommandHandler(ControlConfig config,
    std::unique_ptr<IGimbalDevice> device, Clock clock)
    : config_(std::move(config)), device_(std::move(device)), clock_(std::move(clock))
{
    if (!config_.enabled || !device_) throw std::invalid_argument("Manual control requires enabled control and a device");
    device_->stop(); // Startup only submits a safe stop, never HOME or movement.
}
ManualCommandHandler::~ManualCommandHandler()
{
    if (state_ == State::Moving) {
        try { device_->stop(); }
        catch (const std::exception& e) { Logger::error(std::string("[ManualControl] shutdown stop failed: ") + e.what()); }
    }
}
GuiCommand ManualCommandHandler::initial_state() const
{
    // State describes application control state, not physical encoder feedback.
    return {GuiCategory::System, static_cast<std::uint8_t>(GuiSystem::Status),
            {0, 0, static_cast<std::uint8_t>(state_),
             static_cast<std::uint8_t>(state_ == State::Error ? 1 : 0), 0, 0, 0, 0}};
}
bool ManualCommandHandler::alive() const
{
    return connected_ && clock_() - last_ping_ < std::chrono::milliseconds(config_.heartbeat_timeout_ms);
}
void ManualCommandHandler::connected()
{
    connected_ = true;
    last_ping_ = clock_(); // Initial grace period; only valid PING extends it.
}
void ManualCommandHandler::device_error(const std::exception& error)
{
    Logger::error(std::string("[ManualControl] serial error: ") + error.what());
    try { device_->stop(); }
    catch (const std::exception& e) { Logger::error(std::string("[ManualControl] safe stop failed: ") + e.what()); }
    state_ = State::Error;
    x_ = y_ = 0;
}
void ManualCommandHandler::halt()
{
    if (state_ != State::Moving) return;
    try { device_->stop(); state_ = State::Idle; x_ = y_ = 0; }
    catch (const std::exception& e) { device_error(e); }
}
void ManualCommandHandler::disconnected()
{
    connected_ = false;
    halt(); // No delayed commands are retained for reconnect.
}
void ManualCommandHandler::tick(std::vector<GuiCommand>& replies)
{
    replies.clear();
    const auto previous = state_;
    if (state_ == State::Moving && !alive()) halt();
    if (state_ != previous) replies.push_back(initial_state());
}
int ManualCommandHandler::poll_timeout_ms() const { return config_.poll_interval_ms; }

bool ManualCommandHandler::handle(const GuiCommand& command, std::vector<GuiCommand>& replies)
{
    replies.clear();
    if (command.category == GuiCategory::System) {
        if (command.command != static_cast<std::uint8_t>(GuiSystem::Ping) || !command.payload.empty()) return false;
        last_ping_ = clock_();
        replies.push_back(ack(command, GuiResult::Ok));
        return true;
    }
    if (command.category == GuiCategory::Mode) {
        if (command.command == static_cast<std::uint8_t>(GuiModeCommand::Auto) && command.payload.size() == 4) {
            bool zero = true;
            for (auto byte : command.payload) zero = zero && byte == 0;
            replies.push_back(ack(command, state_ == State::Error || !zero ? GuiResult::NotReady : GuiResult::InvalidTarget));
            return true;
        }
        if (command.command == static_cast<std::uint8_t>(GuiModeCommand::Manual) && command.payload.empty()) {
            // Already MANUAL/None, including during manual movement: idempotent.
            replies.push_back(ack(command, state_ == State::Error ? GuiResult::NotReady : GuiResult::Ok));
            return true;
        }
        return false;
    }
    if (command.category != GuiCategory::Gimbal) return false;
    if (command.command == static_cast<std::uint8_t>(GuiGimbalCommand::Home) && command.payload.empty()) {
        // The PC clears held keys when requesting HOME, without sending MOVE(0,0).
        // Release any current manual motion even though HOME itself is unsupported.
        const auto previous = state_;
        halt();
        replies.push_back(ack(command, GuiResult::NotReady));
        if (state_ != previous) replies.push_back(initial_state());
        return true;
    }
    if (command.command != static_cast<std::uint8_t>(GuiGimbalCommand::Move) || command.payload.size() != 2) return false;
    const int x = direction(command.payload[0]), y = direction(command.payload[1]);
    if (x < -1 || x > 1 || y < -1 || y > 1 || state_ == State::Error) return true;
    // A late/flooded MOVE must not restart movement after heartbeat expiration.
    if ((x || y) && !alive()) return true;
    if (x == x_ && y == y_) return true;
    const auto previous = state_;
    try {
        device_->move(x, y); // Includes MOVE(0,0): release both directional axes.
        x_ = x; y_ = y;
        state_ = x || y ? State::Moving : State::Idle;
        Logger::debug("[ManualControl] MOVE x=" + std::to_string(x) + " y=" + std::to_string(y));
    } catch (const std::exception& e) { device_error(e); }
    if (state_ != previous) replies.push_back(initial_state());
    return true; // MOVE has no ACK, including invalid directions and serial failure.
}
