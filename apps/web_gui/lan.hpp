#pragma once

#include "gui/gui_protocol.hpp"

#include <gio/gio.h>
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace web_gui {
using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;

void append_u32(Bytes& bytes, std::uint32_t value);
std::uint32_t read_u32(const std::uint8_t* bytes);
float read_float(const std::uint8_t* bytes);

struct Change {
    std::uint32_t target;
    std::uint8_t phase;
    std::uint8_t result;
};

// Owns the board's single persistent command connection and correlates replies.
class CommandClient {
public:
    CommandClient(std::string host, unsigned port);
    ~CommandClient();
    Bytes pipeline(std::uint8_t code, Bytes args = {});
    Change change(std::uint8_t code, std::uint32_t target);
    std::optional<Change> change_state(std::uint8_t code);
    void ping();

private:
    GuiCommand exchange(const GuiCommand&, std::uint32_t id);
    void read_exact(std::uint8_t*, std::size_t, Clock::time_point);
    void disconnect();

    std::string host_;
    unsigned port_;
    GSocketConnection* connection_ = nullptr;
    PcGuiProtocol codec_;
    std::mutex mutex_;
    std::uint32_t id_ = 0;
    std::map<std::uint32_t, std::uint8_t> pending_;
    std::map<std::uint8_t, Change> changes_;
};
}
