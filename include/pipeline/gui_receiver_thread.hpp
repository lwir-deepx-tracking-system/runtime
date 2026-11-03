#pragma once

#include <atomic>
#include <pthread.h>
#include <string>

#include "gui/gui_receiver.hpp"

// Owns the TCP session worker; target-following belongs to a future handler strategy.
class GuiReceiverThread
{
private:
    GuiReceiver& receiver_;
    pthread_t thread_{};
    bool started_ = false;
    std::atomic<bool> failed_{false};
    std::string error_message_;

    static void* thread_func(void* arg);
    void run();

public:
    explicit GuiReceiverThread(GuiReceiver& receiver);

    // command 수신 worker를 한 번만 시작한다.
    void start();

    // GuiReceiver의 socket을 닫아 receive() 대기를 깨운다.
    void stop();

    // receive loop가 끝날 때까지 worker 종료를 기다린다.
    void join();
    bool failed() const { return failed_.load(); }
    const std::string& error_message() const { return error_message_; }
};
