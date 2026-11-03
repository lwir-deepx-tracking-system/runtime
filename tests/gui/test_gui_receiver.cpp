#include <chrono>
#include <cerrno>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gui/gui_receiver.hpp"

namespace {
std::vector<std::uint8_t> hex(const std::string& text)
{
    std::vector<std::uint8_t> result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    return result;
}
struct Socket {
    int fd = -1;
    ~Socket() { reset(); }
    void reset(int next = -1) { if (fd >= 0) ::close(fd); fd = next; }
};
std::uint16_t available_port()
{
    Socket socket{::socket(AF_INET, SOCK_STREAM, 0)};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (socket.fd < 0 || ::bind(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))) return 0;
    socklen_t size = sizeof(address);
    if (::getsockname(socket.fd, reinterpret_cast<sockaddr*>(&address), &size)) return 0;
    return ntohs(address.sin_port);
}
int connect_client(std::uint16_t port)
{
    for (int i = 0; i < 100; ++i) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (fd >= 0 && !::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))) return fd;
        if (fd >= 0) ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return -1;
}
bool send_bytes(int fd, const std::vector<std::uint8_t>& bytes)
{
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto n = ::send(fd, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        offset += n;
    }
    return true;
}
bool expect(int fd, const std::vector<std::uint8_t>& expected)
{
    std::vector<std::uint8_t> actual(expected.size());
    std::size_t offset = 0;
    while (offset < actual.size()) {
        pollfd descriptor{fd, POLLIN, 0};
        if (::poll(&descriptor, 1, 2000) <= 0) return false;
        const auto n = ::recv(fd, actual.data() + offset, actual.size() - offset, 0);
        if (n <= 0) return false;
        offset += n;
    }
    return actual == expected;
}
bool disconnected(int fd)
{
    pollfd descriptor{fd, POLLIN, 0};
    if (::poll(&descriptor, 1, 2000) <= 0) return false;
    char byte;
    return ::recv(fd, &byte, 1, 0) == 0;
}
struct Worker {
    GuiReceiver& receiver;
    std::vector<GuiCommand> commands;
    std::thread thread;
    explicit Worker(GuiReceiver& receiver) : receiver(receiver), thread([this] {
        GuiCommand command;
        while (this->receiver.receive(command)) commands.push_back(command);
    }) {}
    ~Worker() { stop(); }
    void stop() { receiver.stop(); if (thread.joinable()) thread.join(); }
};
}

int main()
{
    GuiCommandConfig config;
    config.enabled = true;
    config.bind_address = "127.0.0.1";
    config.port = available_port();
    config.receive_timeout_ms = 20;
    if (!config.port) return 1;
    GuiReceiver receiver(config);
    Worker worker(receiver);
    Socket client{connect_client(config.port)};
    const auto status = hex("4c574952010300080000000000000000");
    if (client.fd < 0 || !expect(client.fd, status)) return 2;
    // Fragment AUTO across header and body boundaries, preserving uint32 IDs.
    const auto auto_command = hex("4c57495202010004ffffffff");
    for (auto byte : auto_command) {
        if (!send_bytes(client.fd, {byte})) return 3;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!expect(client.fd, hex("4c57495201010003020105"))) return 4; // NOT_READY
    // Several commands in one TCP write. MOVE has no ACK and causes no state change.
    if (!send_bytes(client.fd, hex("4c57495201040000" "4c57495203010002ff01"
                                   "4c57495202020000" "4c57495203030000"))) return 5;
    if (!expect(client.fd, hex("4c57495201010003010400" "4c57495201010003020200"
                               "4c57495201010003030305"))) return 6;
    pollfd descriptor{client.fd, POLLIN, 0};
    if (::poll(&descriptor, 1, 50) != 0) return 7; // No invented AUTO/HOME STATUS.
    // Reject declared oversized body immediately, without waiting for it.
    if (!send_bytes(client.fd, hex("4c5749520201ffff")) || !disconnected(client.fd)) return 8;
    client.reset(connect_client(config.port));
    if (client.fd < 0 || !expect(client.fd, status)) return 9;
    if (!send_bytes(client.fd, hex("4c5749520201000400000000")) ||
        !expect(client.fd, hex("4c57495201010003020104"))) return 10; // INVALID_TARGET
    // Reject board-only messages sent in the wrong direction.
    if (!send_bytes(client.fd, status) || !disconnected(client.fd)) return 11;
    client.reset(connect_client(config.port));
    if (client.fd < 0 || !expect(client.fd, status)) return 12;
    // Stop interrupts a partial body as well as poll/accept waits.
    if (!send_bytes(client.fd, hex("4c57495202010004ff"))) return 13;
    const auto before = std::chrono::steady_clock::now();
    worker.stop();
    if (std::chrono::steady_clock::now() - before > std::chrono::seconds(1)) return 14;
    if (worker.commands.size() != 6 || worker.commands[0].payload != hex("ffffffff")) return 15;

    config.port = available_port();
    if (!config.port) return 16;
    GuiReceiver waiting(config);
    Worker waiting_worker(waiting);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    waiting_worker.stop();
    return 0;
}
