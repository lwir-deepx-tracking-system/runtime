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

#include "control/tracking_command_handler.hpp"
#include "pipeline/control_thread.hpp"
#include "gui/gui_receiver.hpp"

namespace {
struct Fd { int value; ~Fd() { if (value >= 0) ::close(value); } };
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
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

// Explicit tracking fixtures exercise the TCP-to-Serial path without inference.
TrackingResultPtr target_frame(std::uint64_t id, float cy = 240) {
    auto f = std::make_shared<FrameContext>();
    f->metadata = {id, std::chrono::steady_clock::now()};
    f->image = cv::Mat::zeros(480,640,CV_16UC1);
    auto r = std::make_shared<TrackingResult>(); r->frame = f;
    Track t; t.track_id = 7; t.x = 470; t.y = cy - 10; t.width = t.height = 20; t.confidence = .9;
    r->tracks.push_back(t);
    return r;
}
template<class Predicate> GuiCommand response(int fd, Predicate predicate) {
    for (int i = 0; i < 30; ++i) {
        auto m = message(fd);
        if (predicate(m)) return m;
    }
    throw std::runtime_error("expected TCP response not received");
}
void ack_result(int fd, GuiResult result) {
    auto m = response(fd, [](const GuiCommand& m) { return m.command == 1; });
    check(m.payload[2] == static_cast<std::uint8_t>(result), "incorrect ACK result");
}
void tcp_auto_serial() {
    Fd master{::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK)};
    check(master.value >= 0 && !::grantpt(master.value) && !::unlockpt(master.value), "AUTO PTY setup");
    ControlConfig config; config.enabled = true;
    config.gimbal.device = ::ptsname(master.value); config.gimbal.x_sign = -1;
    config.heartbeat_timeout_ms = 2000; config.poll_interval_ms = 5;
    config.automatic.period_ms = 5; config.automatic.input_timeout_ms = 200;
    config.automatic.reacquire_timeout_ms = 500;
    config.automatic.speed_min = config.automatic.speed_max = 10;
    RuntimeControl runtime;
    GimbalCommandExecutor executor(config, GimbalDeviceFactory::create(config.gimbal), runtime);
    serial_line(master.value, "{\"T\":135}");
    ThreadSafeQueue<TrackingResultPtr> queue;
    ControlThread control(queue, &executor, false); control.start();
    struct ControlJoin {
        ThreadSafeQueue<TrackingResultPtr>& queue; ControlThread& thread;
        ~ControlJoin() { queue.close(); thread.join(); }
    } control_join{queue, control};
    GuiCommandConfig tcp; tcp.enabled = true; tcp.bind_address = "127.0.0.1"; tcp.port = port();
    GuiReceiver receiver(tcp, GuiCommandHandlerFactory::create(GuiCommandHandlerType::Tracking,config,&executor));
    Worker worker(receiver);
    Fd client{connect_to(tcp.port)};
    check(message(client.value).payload == std::vector<std::uint8_t>(8,0), "initial tracking STATUS");
    send_command(client.value, {GuiCategory::Mode,1,{255,255,255,255}});
    ack_result(client.value,GuiResult::InvalidTarget);
    queue.push(target_frame(1,120)); // above-center target uses the same +Y as manual UP
    // Wait for ControlThread submission, without requesting AUTO repeatedly.
    for (int i = 0; i < 100; ++i) {
        executor.snapshot();
        if (queue.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    send_command(client.value, {GuiCategory::Mode,1,{0,0,0,7}});
    ack_result(client.value,GuiResult::Ok);
    serial_line(master.value,"{\"T\":141,\"X\":-1,\"Y\":1,\"SPD\":10}");
    auto moving = response(client.value, [](const GuiCommand& m) {
        return m.command == 3 && m.payload[0] == 1 && m.payload[2] == 2;
    });
    check(moving.payload[1] == 1 && moving.payload[7] == 7,"AUTO STATUS selection");
    // No tracking frames; TCP remains open and heartbeat is still within deadline.
    serial_line(master.value,"{\"T\":135}");
    response(client.value, [](const GuiCommand& m) { return m.command == 3 && m.payload[1] == 2 && m.payload[2] == 4; });
    // Fresh target reacquires within the grace period.
    queue.push(target_frame(2,120));
    serial_line(master.value,"{\"T\":141,\"X\":-1,\"Y\":1,\"SPD\":10}");
    send_command(client.value,{GuiCategory::Gimbal,1,{0,1}});
    serial_line(master.value,"{\"T\":135}");
    serial_line(master.value,"{\"T\":141,\"X\":0,\"Y\":1,\"SPD\":50}");
    response(client.value, [](const GuiCommand& m) { return m.command == 3 && m.payload[0] == 0 && m.payload[2] == 1 && m.payload[7] == 0; });
    // New tracking data must not revive AUTO after keyboard override.
    queue.push(target_frame(3));
    send_command(client.value,{GuiCategory::Mode,2,{}}); ack_result(client.value,GuiResult::Ok);
    check(!executor.snapshot().automatic,"MANUAL reactivated AUTO");
    ::close(client.value); client.value = -1;
    serial_line(master.value,"{\"T\":135}");
    Fd reconnected{connect_to(tcp.port)};
    check(message(reconnected.value).payload == std::vector<std::uint8_t>(8,0),"reconnect retained selection/motion");
    send_command(reconnected.value,{GuiCategory::Gimbal,3,{}}); ack_result(reconnected.value,GuiResult::NotReady);
    worker.stop();
}
}
int main() try { tcp_auto_serial(); return 0; }
catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
