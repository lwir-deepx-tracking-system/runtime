#pragma once

#include <atomic>
#include <pthread.h>
#include <string>

#include "gui/gui_receiver.hpp"
#include "target/target_selector.hpp"

// GuiReceiver가 수신한 GuiCommand를 TargetSelector 상태에 반영하는 연결 worker.
//
// PC GUI -> GuiReceiver::receive() -> GuiCommand
//   -> GuiReceiverThread -> TargetSelector::set_selected_id()
//   -> ControlThread의 get_selected_id()
//
// socket 처리와 packet decode는 GuiReceiver가 담당한다. 이 클래스는 검증된
// SelectTrack 명령을 제어 경로가 공유하는 선택 ID 상태에 연결하기만 한다.
class GuiReceiverThread
{
private:
    GuiReceiver& receiver_;
    TargetSelector& target_selector_;
    pthread_t thread_{};
    bool started_ = false;
    std::atomic<bool> failed_{false};
    std::string error_message_;

    static void* thread_func(void* arg);
    void run();

public:
    GuiReceiverThread(
        GuiReceiver& receiver,
        TargetSelector& target_selector);

    // command 수신 worker를 한 번만 시작한다.
    void start();

    // GuiReceiver의 socket을 닫아 receive() 대기를 깨운다.
    void stop();

    // receive loop가 끝날 때까지 worker 종료를 기다린다.
    void join();
    bool failed() const { return failed_.load(); }
    const std::string& error_message() const { return error_message_; }
};
