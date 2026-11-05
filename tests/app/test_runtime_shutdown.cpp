#include "runtime/runtime_shutdown.hpp"

// 실제 signal 전달로 저장 종료와 폐기 종료, 폐기 우선순위를 검증한다.
int main() {
    const auto previous_int = std::signal(SIGINT, runtime_shutdown::handle_signal);
    const auto previous_term = std::signal(SIGTERM, runtime_shutdown::handle_signal);
    const auto previous_quit = std::signal(SIGQUIT, runtime_shutdown::handle_signal);
    if (previous_int == SIG_ERR || previous_term == SIG_ERR || previous_quit == SIG_ERR) return 1;
    struct Restore {
        decltype(previous_int) interrupt, terminate, quit;
        ~Restore() {
            std::signal(SIGINT, interrupt);
            std::signal(SIGTERM, terminate);
            std::signal(SIGQUIT, quit);
        }
    } restore{previous_int, previous_term, previous_quit};
    runtime_shutdown::reset();
    std::raise(SIGINT);
    if (!runtime_shutdown::requested || runtime_shutdown::discard_results) return 2;
    runtime_shutdown::reset();
    std::raise(SIGTERM);
    if (!runtime_shutdown::requested || runtime_shutdown::discard_results) return 3;
    runtime_shutdown::reset();
    std::raise(SIGQUIT);
    if (!runtime_shutdown::requested || !runtime_shutdown::discard_results) return 4;
    std::raise(SIGINT);
    runtime_shutdown::request_stop();
    if (!runtime_shutdown::requested || !runtime_shutdown::discard_results) return 5;
    runtime_shutdown::reset();
    runtime_shutdown::request_stop();
    if (!runtime_shutdown::requested || runtime_shutdown::discard_results) return 6;
    return 0;
}
