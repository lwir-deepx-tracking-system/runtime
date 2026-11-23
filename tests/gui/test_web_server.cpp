#include "app/pipeline_requests.hpp"
#include "config/app_config.hpp"
#include "gui/gui_receiver.hpp"
#include "gui/web_server.hpp"

#include <atomic>
#include <httplib.h>
#include <stdexcept>
#include <thread>

namespace {
class Requests final : public IPipelineRequests {
public:
    std::atomic_uint requested_model{0};
    std::uint32_t model_count() const override { return 1; }
    std::uint32_t tracker_count() const override { return 0; }
    PipelineCatalogItem model_info(std::uint32_t id) const override {
        return {id, "Host fixture", true, false};
    }
    PipelineChangeTicket request_model_change(std::uint32_t id) override {
        requested_model = id;
        return {PipelineResult::InvalidId, {}};
    }
    PipelineChangeTicket request_tracker_change(std::uint32_t) override { return {}; }
};

struct ReceiverWorker {
    GuiReceiver receiver;
    std::thread thread;

    ReceiverWorker(const GuiCommandConfig& config, Requests& requests)
        : receiver(config, GuiCommandHandlerFactory::create(
            GuiCommandHandlerType::Deferred, {}, nullptr, &requests)),
          thread([this] {
              GuiCommand command;
              while (receiver.receive(command)) {}
              receiver.finish_session();
          }) {}

    ~ReceiverWorker() {
        receiver.stop();
        thread.join();
    }
};
}

// Repeated ownership checks listener cleanup and forwarding to the real Runtime TCP handler.
int main()
{
    auto config = load_runtime_config(PROJECT_ROOT_DIR "/config/runtime.yaml");
    config.gui.mode = GuiMode::Web;
    config.gui.web.bind_address = "127.0.0.1";
    config.gui.web.port = 39160;
    config.gui.video.port = 39161;
    config.gui.metadata.port = 39162;
    config.gui.command.port = 39163;
    config.gui.command.bind_address = "127.0.0.1";
    config.gui.web.page = PROJECT_ROOT_DIR "/config/runtime.yaml";
    Requests requests;
    ReceiverWorker receiver(config.gui.command, requests);
    for (int i = 0; i < 2; ++i) {
        web_gui::WebServer server(config, {});
        httplib::Client client("127.0.0.1", config.gui.web.port);
        const auto page = client.Get("/");
        if (server.failed() || !page || page->status != 200 ||
            page->body.find("camera_recovery:") == std::string::npos)
            throw std::runtime_error("Runtime-owned HTTP listener failed");

        const auto change = client.Post("/model/set?id=42", {{"X-Gimbal", "1"}}, "", "text/plain");
        if (!change || change->status != 200 || change->body.find("\"result\":1") == std::string::npos ||
            requests.requested_model != 42)
            throw std::runtime_error("HTTP input did not reach the Runtime command handler");
    }
}
