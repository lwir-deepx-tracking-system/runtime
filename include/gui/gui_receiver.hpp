#pragma once

#include <cstddef>
#include <cstdint>

#include <pthread.h>

#include "app/app_config.hpp"
#include "gui/gui_protocol.hpp"

// GUI가 TCP로 보낸 제어 명령을 받는 경계 클래스.
// 영상 수신기가 아니며, RTP/UDP 영상 경로와 연결을 공유하지 않는다.
class GuiReceiver
{
private:
    GuiCommandConfig config_;
    bool stopped_ = false;

    // receive()를 실행하는 worker가 socket을 사용하고, 다른 thread의 stop()이
    // 이를 닫아 blocking poll/recv를 깨운다. mutex는 FD 소유권 인계와
    // double-close를 방지한다.
    pthread_mutex_t socket_mutex_{};
    int listen_fd_ = -1;
    int client_fd_ = -1;

    bool open_listener();
    bool accept_client();
    bool receive_exact(int fd, std::uint8_t* data, std::size_t size);
    void release_client(int fd);
    bool stopped();

public:
    explicit GuiReceiver(GuiCommandConfig config);
    ~GuiReceiver();

    GuiReceiver(const GuiReceiver&) = delete;
    GuiReceiver& operator=(const GuiReceiver&) = delete;

    // 연결이 없으면 GUI 접속을 기다리고, 연결 후에는 고정 크기 packet 하나를
    // 읽어 검증한다. GUI가 끊어지면 내부에서 다시 accept 상태로 돌아간다.
    bool receive(GuiCommand& command);

    // listen/client socket을 shutdown/close해 blocking I/O를 깨운다.
    void stop();
};
