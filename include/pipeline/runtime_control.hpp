#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

enum class RuntimeState
{
    Running,
    Switching,
    SwitchFailed,
    Stopping
};

class RuntimeControl
{
private:
    mutable std::mutex mutex_;  // mutable은 const 함수 내부에서 mutex를 lock, unlock하기 위한 선언이다.
    std::condition_variable condition_;
    bool detection_quiesced_ = false;

public:
    std::atomic<RuntimeState> state{RuntimeState::Running};
    std::atomic<std::uint64_t> generation{0};
    std::atomic<std::uint64_t> tracker_epoch{0};
    std::atomic<bool> stop_requested{false};

    // true는 제출 중단·콜백 반환·탐지 스레드의 파이프라인 참조 해제가 끝났음을 뜻한다.
    void set_detection_quiesced(bool value);
    bool wait_detection_quiesced();
    void request_stop();
};
