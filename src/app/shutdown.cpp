#include "app/shutdown.hpp"

#include <stdexcept>

namespace app_shutdown
{
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
} // namespace app_shutdown
