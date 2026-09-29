#pragma once

#include <pthread.h>

#include "gui/gui_receiver.hpp"
#include "target/target_selector.hpp"

// TCP 명령 수신과 TargetSelector 갱신을 연결하는 worker.
// socket 처리와 packet 검증은 GuiReceiver가 담당하고, 이 thread는 검증이
// 끝난 track_id만 애플리케이션 상태에 반영한다.
class GuiReceiverThread
{
private:
    GuiReceiver& receiver_;
    TargetSelector& target_selector_;
    pthread_t thread_{};
    bool started_ = false;

    static void* thread_func(void* arg);
    void run();

public:
    GuiReceiverThread(
        GuiReceiver& receiver,
        TargetSelector& target_selector);

    void start();
    void stop();
    void join();
};
