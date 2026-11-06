#include "app/shutdown.hpp"

// 실제 signal 전달로 저장 종료와 폐기 종료, 폐기 우선순위를 검증한다.
int check_signals() {
    app_shutdown::SignalScope signals;
    app_shutdown::reset();
    std::raise(SIGINT);
    if (!app_shutdown::requested || app_shutdown::discard_results) return 2;
    app_shutdown::reset();
    std::raise(SIGTERM);
    if (!app_shutdown::requested || app_shutdown::discard_results) return 3;
    app_shutdown::reset();
    std::raise(SIGQUIT);
    if (!app_shutdown::requested || !app_shutdown::discard_results) return 4;
    std::raise(SIGINT);
    app_shutdown::request_stop();
    if (!app_shutdown::requested || !app_shutdown::discard_results) return 5;
    app_shutdown::reset();
    app_shutdown::request_stop();
    if (!app_shutdown::requested || app_shutdown::discard_results) return 6;
    return 0;
}

// 정상·예외 종료 모두 기존 핸들러를 복원하고 다음 실행에서 상태를 초기화한다.
int main() {
    const auto previous = std::signal(SIGINT, SIG_IGN);
    const int result = check_signals();
    const auto restored = std::signal(SIGINT, SIG_IGN);
    std::signal(SIGINT, previous);
    if (result) return result;
    if (restored != SIG_IGN) return 7;
    {
        app_shutdown::SignalScope signals;
        if (app_shutdown::requested || app_shutdown::discard_results) return 8;
    }
    const auto before_exception = std::signal(SIGTERM, SIG_IGN);
    try {
        app_shutdown::SignalScope signals;
        throw 1;
    } catch (int) {}
    const auto after_exception = std::signal(SIGTERM, before_exception);
    if (after_exception != SIG_IGN) return 9;
    return 0;
}
