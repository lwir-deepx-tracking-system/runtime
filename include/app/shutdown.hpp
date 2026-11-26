#pragma once

#include <csignal>

// signal handler는 async-signal-safe한 플래그만 변경하고 I/O·정리는 호출 애플리케이션에 맡긴다.
namespace app_shutdown
{
// 한 프로세스의 최상위 실행 범위 하나가 소유하며 작업 스레드 시작 전에 생성한다.
class SignalScope
{
    using Handler         = void (*)(int);
    const int signals_[3] = {SIGINT, SIGTERM, SIGQUIT};
    Handler   previous_[3]{};
    unsigned  installed_ = 0;

  public:
    SignalScope();
    ~SignalScope();
    SignalScope(const SignalScope &)            = delete;
    SignalScope &operator=(const SignalScope &) = delete;
};
extern volatile std::sig_atomic_t requested;
extern volatile std::sig_atomic_t discard_results;

void reset();
void handle_signal(int signal);
void request_stop(); // 콘솔 quit는 SIGINT와 같은 저장 종료다.
} // namespace app_shutdown
