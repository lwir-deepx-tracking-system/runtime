#include "runtime_console.hpp"

#include <charconv>
#include <chrono>
#include <cerrno>
#include <ostream>
#include <sstream>
#include <poll.h>
#include <unistd.h>

namespace {
const char* result_name(GuiPipelineResult result) {
    switch (result) {
    case GuiPipelineResult::Ok: return "OK";
    case GuiPipelineResult::InvalidId: return "INVALID_ID";
    case GuiPipelineResult::Busy: return "BUSY";
    case GuiPipelineResult::CreationFailed: return "CREATION_FAILED";
    case GuiPipelineResult::Restored: return "RESTORED";
    case GuiPipelineResult::RestoreFailed: return "RESTORE_FAILED";
    case GuiPipelineResult::NotReady: return "NOT_READY";
    }
    return "UNKNOWN";
}
}

RuntimeConsole::RuntimeConsole(IPipelineRequests& requests, std::ostream& output, int input_fd)
    : requests_(requests), output_(output), input_fd_(input_fd) {
    output_ << "[Console] commands: model <id>, tracker <id>, count, help, quit\n" << std::flush;
}

bool RuntimeConsole::execute_line(const std::string& line) {
    if (quitting_) return false;
    std::istringstream input(line);
    std::string command, id_text, extra;
    if (!(input >> command)) return true;
    if (command == "model" || command == "tracker") {
        std::uint32_t id = 0;
        if (!(input >> id_text) || (input >> extra)) {
            output_ << "[Console] usage: " << command << " <positive id>\n";
            return true;
        }
        const auto parsed = std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != id_text.data() + id_text.size() || id == 0) {
            output_ << "[Console] invalid id\n";
            return true;
        }
        if (pending_.size() >= 2) {
            output_ << "[Console] BUSY\n";
            return true;
        }
        auto ticket = command == "model" ? requests_.request_model_change(id) : requests_.request_tracker_change(id);
        if (ticket.result == GuiPipelineResult::Ok && ticket.completion.valid()) {
            const auto label = command + " " + id_text;
            pending_.push_back({label, std::move(ticket.completion)});
            output_ << "[Console] " << label << " accepted\n";
        } else output_ << "[Console] rejected: " << result_name(ticket.result) << '\n';
    } else if (input >> extra) {
        output_ << "[Console] unexpected arguments\n";
    } else if (command == "count") {
        output_ << "[Console] models=" << requests_.model_count() << " trackers=" << requests_.tracker_count() << '\n';
    } else if (command == "quit") {
        quitting_ = true;
    } else {
        output_ << "[Console] commands: model <id>, tracker <id>, count, help, quit\n";
    }
    output_ << std::flush;
    return !quitting_;
}

bool RuntimeConsole::poll() {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
        auto result = GuiPipelineResult::NotReady;
        try { result = it->completion.get(); } catch (const std::future_error&) {}
        output_ << "[Console] " << it->command << " completed: " << result_name(result) << '\n' << std::flush;
        it = pending_.erase(it);
    }
    if (!input_open_ || quitting_) return !quitting_;
    // C++17에는 stdin 비차단 API가 없어 이 경계에서만 poll/read를 사용한다.
    pollfd descriptor{input_fd_, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, 0);
    if (ready < 0) {
        if (errno != EINTR) input_open_ = false;
        return true;
    }
    if (!ready) return true;
    if (descriptor.revents & (POLLIN | POLLHUP)) {
        char bytes[256];
        const auto count = ::read(input_fd_, bytes, sizeof(bytes));
        if (count == 0) {
            input_open_ = false;
            if (!discarding_ && !line_.empty()) execute_line(line_);
            line_.clear();
        } else if (count > 0) {
            for (ssize_t i = 0; i < count && !quitting_; ++i) {
                if (bytes[i] == '\n') {
                    if (!discarding_) execute_line(line_);
                    line_.clear(); discarding_ = false;
                } else if (!discarding_) {
                    if (line_.size() >= 256) {
                        output_ << "[Console] line too long\n" << std::flush;
                        line_.clear(); discarding_ = true;
                    } else line_.push_back(bytes[i]);
                }
            }
        } else if (errno != EINTR && errno != EAGAIN) input_open_ = false;
    } else if (descriptor.revents & (POLLERR | POLLNVAL)) input_open_ = false;
    return !quitting_;
}
