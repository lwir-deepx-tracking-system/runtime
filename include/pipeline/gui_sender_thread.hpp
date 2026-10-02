#pragma once

#include <pthread.h>

#include "common/threadsafequeue.hpp"
#include "common/track.hpp"
#include "gui/gui_sender.hpp"

// gui_track_queue의 TrackingResult를 소비해 GuiSender::send()로 넘기는 연결 worker.
//
// TrackingThread -> gui_track_queue -> GuiSenderThread -> GuiSender
//
// 영상 변환, H.264/RTP 송신, metadata 직렬화는 수행하지 않는다. 실제 네트워크
// 처리는 GuiSender가 담당하고 이 클래스는 queue 소비와 pthread 수명만 관리한다.
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

    // queue 소비 worker를 한 번만 시작한다.
    void start();

    // TrackingThread가 queue를 닫고 남은 결과가 처리될 때까지 기다린다.
    void join();
};
