#include "app/shutdown.hpp"

#include <stdexcept>

namespace app_shutdown
{
volatile std::sig_atomic_t requested = 0;
volatile std::sig_atomic_t discard_results = 0;

// 프로세스 단위 종료 신호를 설치하고 등록 실패 시 이미 바꾼 핸들러를 복구한다.
SignalScope::SignalScope()
{
    reset();
    for(; installed_ < 3; ++installed_)
    {
        previous_[installed_] = std::signal(signals_[installed_], handle_signal);
        if(previous_[installed_] == SIG_ERR)
        {
            while(installed_)
            {
                --installed_;
                std::signal(signals_[installed_], previous_[installed_]);
            }
            throw std::runtime_error("Application signal handler 등록 실패");
        }
    }
}
// 실행 범위가 끝나면 호출자가 사용하던 신호 처리기를 되돌린다.
SignalScope::~SignalScope()
{
    while(installed_)
    {
        --installed_;
        std::signal(signals_[installed_], previous_[installed_]);
    }
}

void reset()
{
    requested       = 0;
    discard_results = 0;
}

void handle_signal(int signal)
{
    if(signal == SIGQUIT) discard_results = 1;
    requested = 1; // 이후 SIGINT/SIGTERM을 받아도 SIGQUIT의 폐기 요청은 유지한다.
}

void request_stop() { requested = 1; }

}  // namespace app_shutdown
