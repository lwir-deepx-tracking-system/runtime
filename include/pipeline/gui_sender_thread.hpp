#pragma once

#include <pthread.h>

#include "common/threadsafequeue.hpp"
#include "common/track.hpp"
#include "gui/gui_sender.hpp"

// 최신 TrackingResult를 GuiSender에 전달하는 worker.
// 실제 H.264/RTP/UDP 처리는 GuiSender가 소유하며, 이 thread는 queue의 오래된
// 프레임이 네트워크 전송 책임과 섞이지 않도록 실행 흐름만 담당한다.
class GuiSenderThread
{
private:
    GuiSender& sender_;
    ThreadSafeQueue<TrackingResultPtr>& input_queue_;
    pthread_t thread_{};
    bool started_ = false;

    static void* thread_func(void* arg);
    void run();

public:
    GuiSenderThread(
        GuiSender& sender,
        ThreadSafeQueue<TrackingResultPtr>& input_queue);

    void start();
    void join();
};
