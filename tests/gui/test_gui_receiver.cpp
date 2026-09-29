#include <cstdint>
#include <ctime>

#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gui/gui_protocol.hpp"
#include "gui/gui_receiver.hpp"

namespace {
void sleep_milliseconds(long milliseconds)
{
    timespec duration{};
    duration.tv_sec = milliseconds / 1000;
    duration.tv_nsec = (milliseconds % 1000) * 1000000L;
    while (::nanosleep(&duration, &duration) != 0)
    {
    }
}

std::uint16_t find_available_loopback_port()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return 0;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        return 0;
    }

    socklen_t size = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) != 0)
    {
        ::close(fd);
        return 0;
    }
    const std::uint16_t port = ntohs(address.sin_port);
    ::close(fd);
    return port;
}

int connect_with_retry(std::uint16_t port)
{
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd < 0) return -1;

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (::connect(
                fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0)
            return fd;
        ::close(fd);
        sleep_milliseconds(10);
    }
    return -1;
}

struct ReceiveTask
{
    GuiReceiver* receiver = nullptr;
    GuiCommand command;
    bool result = false;
};

void* receive_command(void* argument)
{
    auto* task = static_cast<ReceiveTask*>(argument);
    task->result = task->receiver->receive(task->command);
    return nullptr;
}
}

int main()
{
    GuiCommandConfig config;
    config.bind_address = "127.0.0.1";
    config.port = find_available_loopback_port();
    config.receive_timeout_ms = 20;
    if (config.port == 0) return 1;

    GuiReceiver receiver(config);
    ReceiveTask pending{&receiver};
    pthread_t pending_thread{};
    if (pthread_create(
            &pending_thread, nullptr, &receive_command, &pending) != 0)
        return 2;

    const int client = connect_with_retry(config.port);
    if (client < 0)
    {
        receiver.stop();
        pthread_join(pending_thread, nullptr);
        return 3;
    }

    const GuiCommand command{GuiCommandType::SelectTrack, 17};
    const auto packet = encode_gui_command(command);
    const ssize_t sent = ::send(client, packet.data(), packet.size(), 0);
    ::close(client);
    pthread_join(pending_thread, nullptr);
    if (sent != static_cast<ssize_t>(packet.size())) return 4;
    if (!pending.result || pending.command.track_id != 17) return 5;

    // 연결을 기다리는 중에도 stop()이 poll을 깨우고 thread를 종료해야 한다.
    ReceiveTask waiting{&receiver};
    pthread_t waiting_thread{};
    if (pthread_create(
            &waiting_thread, nullptr, &receive_command, &waiting) != 0)
        return 6;
    sleep_milliseconds(30);
    receiver.stop();
    pthread_join(waiting_thread, nullptr);
    if (waiting.result) return 7;

    return 0;
}
