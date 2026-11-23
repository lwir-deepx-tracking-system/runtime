#pragma once

#include <filesystem>
#include <memory>

struct RuntimeConfig;

namespace web_gui {
// Runtime-owned HTTP listener and loopback PC protocol client.
class WebServer {
public:
    WebServer(const RuntimeConfig&, const std::filesystem::path& recordings);
    ~WebServer();
    bool failed() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
