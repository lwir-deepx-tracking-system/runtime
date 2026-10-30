#pragma once

#include <cstddef>
#include <cstdint>

#include <pthread.h>

#include "config/app_config.hpp"
#include "gui/gui_protocol.hpp"

// PC GUI -> Orange Pi 방향의 Track 선택 명령을 TCP로 수신한다.
//
// 전체 흐름:
// listen socket 생성 -> GUI client 연결 대기 -> 고정 크기 packet 수신
//   -> decode_gui_command() 검증 -> GuiCommand 반환
//
// 영상/metadata 송신 경로와는 독립된 command 채널이며, 선택 ID를 직접
// 저장하지 않는다. 네트워크 수신과 protocol 검증까지만 담당하고 실제 상태
// 반영은 GuiReceiverThread가 TargetSelector에 수행한다.
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

    // 설정 주소에 TCP listen socket을 열어 GUI 재접속을 받을 준비를 한다.
    bool open_listener();

    // poll timeout마다 종료 상태를 확인하면서 GUI client 연결을 기다린다.
    bool accept_client();

    // TCP가 packet 경계를 보존하지 않으므로 command 고정 크기만큼 누적 수신한다.
    bool receive_exact(int fd, std::uint8_t* data, std::size_t size);

    // 현재 receive 경로가 FD 소유권을 가진 경우에만 client socket을 닫는다.
    void release_client(int fd);

    // receive worker와 stop 호출자 사이에서 종료 상태를 안전하게 조회한다.
    bool stopped();

public:
    explicit GuiReceiver(GuiCommandConfig config);
    ~GuiReceiver();

    GuiReceiver(const GuiReceiver&) = delete;
    GuiReceiver& operator=(const GuiReceiver&) = delete;

    // 연결이 없으면 GUI 접속을 기다리고, 연결 후 고정 크기 command 하나를
    // 읽어 검증한다. 연결이 끊기거나 잘못된 stream이면 client만 닫고 새 GUI의
    // 재접속을 기다린다.
    bool receive(GuiCommand& command);

    // listen/client socket을 shutdown/close해 다른 thread의 poll/recv를 깨운다.
    void stop();

    bool stop_requested() { return stopped(); }
};
