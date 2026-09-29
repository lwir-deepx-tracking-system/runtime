#include "gui/gui_receiver.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/logger.hpp"

namespace {
// pthread_mutex_t를 빠뜨리지 않고 unlock하기 위한 작은 RAII 보조 객체이다.
// 실제 동기화 primitive와 lock/unlock 호출은 모두 POSIX pthread API를 쓴다.
class PthreadLockGuard
{
private:
    pthread_mutex_t* mutex_;

public:
    explicit PthreadLockGuard(pthread_mutex_t& mutex) : mutex_(&mutex)
    {
        pthread_mutex_lock(mutex_);
    }

    ~PthreadLockGuard()
    {
        pthread_mutex_unlock(mutex_);
    }

    PthreadLockGuard(const PthreadLockGuard&) = delete;
    PthreadLockGuard& operator=(const PthreadLockGuard&) = delete;
};

void close_socket(int fd)
{
    if (fd < 0) return;
    // shutdown()이 다른 thread에서 대기 중인 poll/recv/accept를 깨우고,
    // close()가 process의 FD 소유권을 최종 해제한다.
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
}
}

GuiReceiver::GuiReceiver(GuiCommandConfig config)
    : config_(std::move(config))
{
    if (pthread_mutex_init(&socket_mutex_, nullptr) != 0)
        throw std::runtime_error("GuiReceiver mutex 초기화 실패");
}

GuiReceiver::~GuiReceiver()
{
    stop();
    pthread_mutex_destroy(&socket_mutex_);
}

bool GuiReceiver::stopped()
{
    PthreadLockGuard lock(socket_mutex_);
    return stopped_;
}

bool GuiReceiver::open_listener()
{
    {
        PthreadLockGuard lock(socket_mutex_);
        if (listen_fd_ >= 0) return true;
        if (stopped_) return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* addresses = nullptr;
    const std::string port = std::to_string(config_.port);
    const int lookup = ::getaddrinfo(
        config_.bind_address.c_str(), port.c_str(), &hints, &addresses);
    if (lookup != 0)
    {
        Logger::error(
            "[GuiReceiver] bind 주소 해석 실패: " +
            std::string(gai_strerror(lookup)));
        return false;
    }

    int listener = -1;
    for (addrinfo* address = addresses; address; address = address->ai_next)
    {
        const int fd = ::socket(
            address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;

        const int reuse = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (::bind(fd, address->ai_addr, address->ai_addrlen) == 0 &&
            ::listen(fd, 1) == 0)
        {
            listener = fd;
            break;
        }
        ::close(fd);
    }
    ::freeaddrinfo(addresses);

    if (listener < 0)
    {
        Logger::error(
            "[GuiReceiver] TCP listen 실패: " + config_.bind_address + ":" +
            port + " (" + std::strerror(errno) + ")");
        return false;
    }

    {
        PthreadLockGuard lock(socket_mutex_);
        if (stopped_)
        {
            close_socket(listener);
            return false;
        }
        listen_fd_ = listener;
    }
    Logger::info(
        "[GuiReceiver] TCP 명령 대기: " + config_.bind_address + ":" + port);
    return true;
}

bool GuiReceiver::accept_client()
{
    while (!stopped())
    {
        int listener = -1;
        {
            PthreadLockGuard lock(socket_mutex_);
            if (client_fd_ >= 0) return true;
            listener = listen_fd_;
        }
        if (listener < 0) return false;

        pollfd descriptor{};
        descriptor.fd = listener;
        descriptor.events = POLLIN;
        const int ready = ::poll(
            &descriptor, 1, config_.receive_timeout_ms);
        if (ready == 0) continue;
        if (ready < 0)
        {
            if (errno == EINTR) continue;
            if (!stopped())
                Logger::error("[GuiReceiver] TCP accept 대기 실패");
            return false;
        }
        if ((descriptor.revents & POLLIN) == 0)
            return false;

        const int client = ::accept(listener, nullptr, nullptr);
        if (client < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            if (!stopped())
                Logger::error("[GuiReceiver] TCP client accept 실패");
            return false;
        }

        {
            PthreadLockGuard lock(socket_mutex_);
            if (stopped_)
            {
                close_socket(client);
                return false;
            }
            client_fd_ = client;
        }
        Logger::info("[GuiReceiver] GUI 명령 연결 수립");
        return true;
    }
    return false;
}

bool GuiReceiver::receive_exact(
    int fd,
    std::uint8_t* data,
    std::size_t size)
{
    std::size_t received = 0;
    while (received < size && !stopped())
    {
        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = POLLIN;
        const int ready = ::poll(
            &descriptor, 1, config_.receive_timeout_ms);
        if (ready == 0) continue;
        if (ready < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }

        if ((descriptor.revents & POLLIN) != 0)
        {
            const ssize_t count = ::recv(
                fd, data + received, size - received, 0);
            if (count > 0)
            {
                received += static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            return false;
        }

        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            return false;
    }
    return received == size;
}

void GuiReceiver::release_client(int fd)
{
    bool owns_fd = false;
    {
        PthreadLockGuard lock(socket_mutex_);
        if (client_fd_ == fd)
        {
            client_fd_ = -1;
            owns_fd = true;
        }
    }
    // stop()이 이미 FD를 가져가 닫았다면 여기서 다시 닫지 않는다.
    if (owns_fd) close_socket(fd);
}

bool GuiReceiver::receive(GuiCommand& command)
{
    if (!config_.enabled || stopped()) return false;
    if (!open_listener()) return false;

    std::array<std::uint8_t, kGuiCommandPacketSize> packet{};
    while (!stopped())
    {
        if (!accept_client()) return false;

        int client = -1;
        {
            PthreadLockGuard lock(socket_mutex_);
            client = client_fd_;
        }
        if (client < 0) continue;

        if (!receive_exact(client, packet.data(), packet.size()))
        {
            release_client(client);
            if (!stopped())
                Logger::warn("[GuiReceiver] GUI 연결 종료, 재접속 대기");
            continue;
        }

        if (!decode_gui_command(packet.data(), packet.size(), command))
        {
            // 고정 길이 protocol이 어긋나면 stream 경계를 복원할 수 없으므로
            // 연결을 닫고 새 연결에서 다시 시작한다.
            Logger::warn("[GuiReceiver] 유효하지 않은 GUI 명령 수신");
            release_client(client);
            continue;
        }
        return true;
    }
    return false;
}

void GuiReceiver::stop()
{
    int listener = -1;
    int client = -1;
    {
        PthreadLockGuard lock(socket_mutex_);
        if (stopped_) return;
        stopped_ = true;
        listener = listen_fd_;
        client = client_fd_;
        listen_fd_ = -1;
        client_fd_ = -1;
    }
    close_socket(client);
    close_socket(listener);
}
