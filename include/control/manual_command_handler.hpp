#pragma once

#include <chrono>
#include <functional>
#include "control/gimbal_device.hpp"
#include "gui/gui_command_handler.hpp"

// Single owner of manual state; all methods except destruction run on the TCP
// worker. It never consumes tracking frames or changes TargetSelector.
class ManualCommandHandler final : public IGuiCommandHandler {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Clock = std::function<TimePoint()>;
    ManualCommandHandler(ControlConfig, std::unique_ptr<IGimbalDevice>,
                         Clock clock = std::chrono::steady_clock::now);
    ~ManualCommandHandler() override;
    GuiCommand initial_state() const override;
    bool handle(const GuiCommand&, std::vector<GuiCommand>&) override;
    void connected() override;
    void disconnected() override;
    void tick(std::vector<GuiCommand>&) override;
    int poll_timeout_ms() const override;
private:
    enum class State : std::uint8_t { Idle = 0, Moving = 1, Error = 5 };
    ControlConfig config_;
    std::unique_ptr<IGimbalDevice> device_;
    Clock clock_;
    TimePoint last_ping_{};
    State state_ = State::Idle;
    bool connected_ = false;
    int x_ = 0, y_ = 0;
    bool alive() const;
    void halt();
    void device_error(const std::exception&);
};
