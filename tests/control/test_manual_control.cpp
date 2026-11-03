#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "control/manual_command_handler.hpp"
#include "gui/gui_receiver.hpp"

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fd {
    int value;
    ~Fd() { if (value >= 0) ::close(value); }
};
class RecordingDevice final : public IGimbalDevice {
public:
    std::vector<std::string> events;
    bool fail_move = false;
    void move(int x, int y) override {
        if (fail_move) throw std::runtime_error("test serial failure");
        events.push_back(std::to_string(x) + "," + std::to_string(y));
    }
    void stop() override { events.push_back("stop"); }
    void home() override { events.push_back("home"); }
};
GuiCommand move(std::uint8_t x, std::uint8_t y) { return {GuiCategory::Gimbal, 1, {x, y}}; }
void state_machine()
{
    ControlConfig config;
    config.enabled = true;
    config.heartbeat_timeout_ms = 200;
    try {
        GuiCommandHandlerFactory::create(GuiCommandHandlerType::Manual);
        throw std::runtime_error("disabled control accepted manual handler");
    } catch (const std::runtime_error& error) {
        check(std::string(error.what()) != "disabled control accepted manual handler", "disabled factory accepted manual");
    }
    auto now = ManualCommandHandler::TimePoint{};
    auto device = std::make_unique<RecordingDevice>();
    auto* record = device.get();
    ManualCommandHandler handler(config, std::move(device), [&] { return now; });
    check(record->events == std::vector<std::string>{"stop"}, "startup must stop, not move/home");
    handler.connected();
    std::vector<GuiCommand> replies;
    handler.handle(move(255, 1), replies);
    check(record->events.back() == "-1,1" && replies.size() == 1 &&
          replies[0].payload[2] == 1, "signed diagonal MOVE or Moving STATUS incorrect");
    handler.handle(move(255, 1), replies);
    check(record->events.size() == 2 && replies.empty(), "duplicate MOVE should not repeat serial writes");
    handler.handle(move(2, 0), replies);
    check(record->events.size() == 2 && replies.empty(), "invalid direction executed or acknowledged");
    handler.handle({GuiCategory::Mode, 2, {}}, replies);
    check(replies.size() == 1 && replies[0].payload[2] == 0 &&
          handler.initial_state().payload[2] == 1, "idempotent MANUAL changed manual movement");
    handler.handle(move(0, 0), replies);
    check(record->events.back() == "0,0" && replies.size() == 1 && replies[0].payload[2] == 0,
          "key release must submit both zero directions and Idle STATUS");
    handler.handle(move(1, 0), replies);
    now += std::chrono::milliseconds(150);
    handler.handle({GuiCategory::System, 4, {}}, replies);
    check(replies.size() == 1 && replies[0].payload == std::vector<std::uint8_t>({1,4,0}), "PING ACK incorrect");
    now += std::chrono::milliseconds(150);
    handler.tick(replies);
    check(replies.empty() && record->events.back() == "1,0", "valid PING did not extend liveness");
    now += std::chrono::milliseconds(51);
    handler.tick(replies);
    check(record->events.back() == "stop" && replies.size() == 1 && replies[0].payload[2] == 0,
          "heartbeat did not stop movement/report Idle");
    const auto stopped_count = record->events.size();
    handler.handle(move(1, 0), replies);
    check(record->events.size() == stopped_count, "stale MOVE restarted without valid PING");
    handler.handle({GuiCategory::System, 4, {}}, replies);
    handler.handle(move(1, 0), replies);
    handler.disconnected();
    check(record->events.back() == "stop", "disconnect did not stop");
    handler.connected();
    check(handler.initial_state().payload[2] == 0 && record->events.back() == "stop", "reconnect replayed movement");
    handler.handle({GuiCategory::Mode, 1, {255,255,255,255}}, replies);
    check(replies[0].payload[2] == 5, "AUTO must remain NOT_READY with full uint32 ID");
    handler.handle({GuiCategory::Gimbal, 3, {}}, replies);
    check(replies[0].payload[2] == 5 && record->events.back() != "home", "HOME executed without feedback support");
    handler.handle(move(0, 1), replies);
    handler.handle({GuiCategory::Gimbal, 3, {}}, replies);
    check(record->events.back() == "stop" && replies.size() == 2 &&
          replies[0].payload[2] == 5 && replies[1].payload[2] == 0,
          "unsupported HOME left motion active after PC cleared held keys");
    record->fail_move = true;
    handler.handle(move(1, 1), replies);
    check(replies.size() == 1 && replies[0].command == 3 && replies[0].payload[2] == 5 &&
          replies[0].payload[3] == 1 && record->events.back() == "stop", "serial failure not reported as ERROR");
    handler.handle({GuiCategory::Mode, 2, {}}, replies);
    check(replies[0].payload[2] == 5, "error state accepted a control request");
    handler.handle({GuiCategory::System, 4, {}}, replies);
    check(replies[0].payload[2] == 0, "serial error must not disable PING");
}
std::uint16_t port()
{
    Fd socket{::socket(AF_INET, SOCK_STREAM, 0)};
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(socket.value >= 0 && !::bind(socket.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)), "test port bind");
    socklen_t size = sizeof(address);
    check(!::getsockname(socket.value, reinterpret_cast<sockaddr*>(&address), &size), "test getsockname");
    return ntohs(address.sin_port);
}
int connect_to(std::uint16_t port)
{
    for (int i = 0; i < 100; ++i) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(port);
        if (fd >= 0 && !::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))) return fd;
        if (fd >= 0) ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("TCP test connect");
}
std::vector<std::uint8_t> read_bytes(int fd, std::size_t size)
{
    std::vector<std::uint8_t> bytes(size);
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (offset < size) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        check(remaining > 0, "test read overall timeout");
        pollfd p{fd, POLLIN, 0};
        const int ready = ::poll(&p, 1, static_cast<int>(remaining));
        if (ready < 0 && errno == EINTR) continue;
        check(ready > 0, "test read timeout");
        const auto count = ::read(fd, bytes.data() + offset, size - offset);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        check(count > 0, "test read failed");
        offset += count;
    }
    return bytes;
}
void serial_line(int fd, const std::string& expected)
{
    auto bytes = read_bytes(fd, expected.size() + 1);
    check(std::string(bytes.begin(), bytes.end()) == expected + "\n", "incorrect vendor serial line");
}
GuiCommand message(int fd)
{
    auto bytes = read_bytes(fd, 8);
    PcGuiProtocol codec;
    const auto size = codec.command_packet_size(bytes.data(), bytes.size());
    check(size >= 8, "bad response framing");
    auto body = read_bytes(fd, size - 8);
    bytes.insert(bytes.end(), body.begin(), body.end());
    GuiCommand decoded;
    check(codec.decode_command(bytes.data(), bytes.size(), decoded), "bad response decode");
    return decoded;
}
void send_command(int fd, const GuiCommand& command)
{
    const auto bytes = encode_gui_command(command);
    check(::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(bytes.size()), "test command send");
}
struct Worker {
    GuiReceiver& receiver;
    std::thread thread;
    explicit Worker(GuiReceiver& r) : receiver(r), thread([&r] {
        GuiCommand command;
        while (r.receive(command)) {}
    }) {}
    void stop() { receiver.stop(); if (thread.joinable()) thread.join(); }
    ~Worker() { stop(); }
};
void serial_deadline()
{
    Fd master{::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK)};
    check(master.value >= 0 && !::grantpt(master.value) && !::unlockpt(master.value), "deadline PTY setup");
    GimbalConfig config;
    config.device = ::ptsname(master.value);
    config.write_timeout_ms = 15;
    auto device = GimbalDeviceFactory::create(config);
    const auto started = std::chrono::steady_clock::now();
    bool failed = false;
    // Do not drain the PTY: a backed-up device must not block the TCP worker forever.
    for (int i = 0; i < 10000; ++i) {
        try { device->move(1, 0); }
        catch (const std::runtime_error&) { failed = true; break; }
    }
    check(failed, "serial backlog did not reach a bounded write failure");
    check(std::chrono::steady_clock::now() - started < std::chrono::seconds(2), "serial deadline was not bounded");
}
void tcp_serial()
{
    Fd master{::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK)};
    check(master.value >= 0 && !::grantpt(master.value) && !::unlockpt(master.value), "PTY setup");
    ControlConfig config;
    config.enabled = true;
    config.heartbeat_timeout_ms = 250;
    config.poll_interval_ms = 10;
    config.gimbal.device = ::ptsname(master.value);
    config.gimbal.speed = 37; config.gimbal.acceleration = 61;
    config.gimbal.x_sign = -1;
    config.gimbal.home_x_deg = 12; config.gimbal.home_y_deg = -5;
    {
        auto gimbal = GimbalDeviceFactory::create(config.gimbal);
        gimbal->home();
        serial_line(master.value, "{\"T\":133,\"X\":12,\"Y\":-5,\"SPD\":37,\"ACC\":61}");
    }
    auto handler = GuiCommandHandlerFactory::create(GuiCommandHandlerType::Manual, config);
    serial_line(master.value, "{\"T\":135}");
    GuiCommandConfig tcp;
    tcp.enabled = true; tcp.bind_address = "127.0.0.1"; tcp.port = port();
    tcp.receive_timeout_ms = 1000; // Handler must poll on its own shorter interval.
    GuiReceiver receiver(tcp, std::move(handler));
    Worker worker(receiver);
    Fd client{connect_to(tcp.port)};
    check(message(client.value).payload[2] == 0, "initial STATUS");
    send_command(client.value, move(255, 1));
    serial_line(master.value, "{\"T\":141,\"X\":1,\"Y\":1,\"SPD\":37}");
    check(message(client.value).payload[2] == 1, "MOVING status");
    send_command(client.value, move(0, 0));
    serial_line(master.value, "{\"T\":141,\"X\":0,\"Y\":0,\"SPD\":37}");
    check(message(client.value).payload[2] == 0, "key release status");
    send_command(client.value, move(1, 0));
    serial_line(master.value, "{\"T\":141,\"X\":-1,\"Y\":0,\"SPD\":37}");
    check(message(client.value).payload[2] == 1, "moving status before partial command");
    // Freeze mid-body while moving: heartbeat still needs to STOP without frames.
    const std::uint8_t partial[] = {0x4c,0x57,0x49,0x52,2,1,0,4,0xff};
    check(::send(client.value, partial, sizeof(partial), MSG_NOSIGNAL) == sizeof(partial), "partial send");
    serial_line(master.value, "{\"T\":135}");
    check(message(client.value).payload[2] == 0, "heartbeat status during partial TCP body");
    ::close(client.value); client.value = -1;
    Fd reconnected{connect_to(tcp.port)};
    check(message(reconnected.value).payload[2] == 0, "reconnect snapshot");
    send_command(reconnected.value, move(0, 1));
    serial_line(master.value, "{\"T\":141,\"X\":0,\"Y\":1,\"SPD\":37}");
    check(message(reconnected.value).payload[2] == 1, "moving before disconnect");
    ::close(reconnected.value); reconnected.value = -1;
    serial_line(master.value, "{\"T\":135}");
    Fd final_client{connect_to(tcp.port)};
    check(message(final_client.value).payload[2] == 0, "post-disconnect snapshot");
    send_command(final_client.value, move(1, 0));
    serial_line(master.value, "{\"T\":141,\"X\":-1,\"Y\":0,\"SPD\":37}");
    check(message(final_client.value).payload[2] == 1, "moving before shutdown");
    worker.stop();
    serial_line(master.value, "{\"T\":135}");
}
}
int main()
try { state_machine(); serial_deadline(); tcp_serial(); return 0; }
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
