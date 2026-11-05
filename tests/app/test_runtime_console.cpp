#include "runtime/runtime_console.hpp"
#include <sstream>
#include <unistd.h>

namespace {
// 실제 추론 없이 콘솔의 명령 분기와 완료 응답을 관찰한다.
class Requests final : public IPipelineRequests {
public:
    std::promise<GuiPipelineResult> model, tracker;
    std::shared_future<GuiPipelineResult> model_future = model.get_future().share();
    std::shared_future<GuiPipelineResult> tracker_future = tracker.get_future().share();
    unsigned model_calls = 0, tracker_calls = 0;
    std::uint32_t model_count() const override { return 2; }
    std::uint32_t tracker_count() const override { return 1; }
    PipelineChangeTicket request_model_change(std::uint32_t id) override {
        ++model_calls;
        if (id > 2) return {GuiPipelineResult::InvalidId, {}};
        return {GuiPipelineResult::Ok, model_future};
    }
    PipelineChangeTicket request_tracker_change(std::uint32_t id) override {
        ++tracker_calls;
        if (id > 1) return {GuiPipelineResult::InvalidId, {}};
        return {GuiPipelineResult::Ok, tracker_future};
    }
};
}

// 잘못된 ID/추가 인자 거부, 비동기 완료, 분할 입력과 EOF 처리를 검증한다.
int main() {
    Requests requests;
    std::ostringstream output;
    RuntimeConsole console(requests, output, -1);
    for (const auto* line : {"model", "tracker -1", "model 0", "model 4294967296",
            "model 1x", "model +1", "tracker 1 extra", "count extra", "quit extra"})
        if (!console.execute_line(line)) return 1;
    if (requests.model_calls || requests.tracker_calls) return 2;
    console.execute_line("count");
    console.execute_line("model 99");
    console.execute_line("model 2");
    console.execute_line("tracker 1");
    requests.model.set_value(GuiPipelineResult::Restored);
    requests.tracker.set_value(GuiPipelineResult::Ok);
    if (!console.poll() || requests.model_calls != 2 || requests.tracker_calls != 1) return 3;
    for (const auto* expected : {"models=2 trackers=1", "INVALID_ID", "model 2 accepted",
            "model 2 completed: RESTORED", "tracker 1 completed: OK"})
        if (output.str().find(expected) == std::string::npos) return 4;

    int fds[2];
    if (::pipe(fds)) return 5;
    RuntimeConsole piped(requests, output, fds[0]);
    if (::write(fds[1], "cou", 3) != 3 || !piped.poll()) return 6;
    if (::write(fds[1], "nt\nhelp\n", 8) != 8 || !piped.poll()) return 7;
    const auto oversized = std::string(270, 'a') + "\n";
    if (::write(fds[1], oversized.data(), oversized.size()) != static_cast<ssize_t>(oversized.size())) return 8;
    if (!piped.poll() || !piped.poll() || output.str().find("line too long") == std::string::npos) return 9;
    if (::write(fds[1], "count", 5) != 5) return 10;
    ::close(fds[1]);
    if (!piped.poll() || !piped.poll()) return 11; // EOF는 runtime 종료가 아니다.
    ::close(fds[0]);
    if (console.execute_line("quit")) return 12;
    return 0;
}
