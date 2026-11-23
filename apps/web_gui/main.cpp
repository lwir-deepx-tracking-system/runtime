#include "video.hpp"

#include <glib-unix.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace web_gui {
namespace {
using Json = nlohmann::json;
using Request = httplib::Request;
using Response = httplib::Response;

std::uint32_t number(std::string_view text)
{
    std::uint32_t value = 0;
    const auto end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0)
        throw std::invalid_argument("Expected a positive integer");
    return value;
}

std::vector<std::uint32_t> ids(std::string_view text)
{
    std::vector<std::uint32_t> result;
    for (;;) {
        const auto comma = text.find(',');
        result.push_back(number(text.substr(0, comma)));
        if (comma == text.npos) return result;
        text.remove_prefix(comma + 1);
    }
}

struct Settings {
    std::string bind = "127.0.0.1";
    std::string board = "127.0.0.1";
    std::string page = PROJECT_ROOT_DIR "/../model-bank/tracking_server/page/live_page.html";
    unsigned http_port = 8080;
    unsigned video_port = 5000;
    unsigned metadata_port = 5001;
    unsigned command_port = 5002;
    bool jpeg = false;
    std::vector<std::uint32_t> model_ids = ids("1,2,3,4,5,6,7,8,9,10,11,12,13");
    std::vector<std::uint32_t> tracker_ids = ids("1,2,3,4,5,6,7");
};

unsigned port(std::string_view text)
{
    const auto value = number(text);
    if (value > 65535) throw std::invalid_argument("Port exceeds 65535");
    return value;
}

Settings arguments(int argc, char** argv)
{
    Settings settings;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (++index == argc) throw std::invalid_argument("Missing value for " + std::string(option));
        const std::string_view value = argv[index];
        if (option == "--bind") settings.bind = value;
        else if (option == "--board") settings.board = value;
        else if (option == "--page") settings.page = value;
        else if (option == "--http-port") settings.http_port = port(value);
        else if (option == "--video-port") settings.video_port = port(value);
        else if (option == "--metadata-port") settings.metadata_port = port(value);
        else if (option == "--command-port") settings.command_port = port(value);
        else if (option == "--model-ids") settings.model_ids = ids(value);
        else if (option == "--tracker-ids") settings.tracker_ids = ids(value);
        else if (option == "--codec" && (value == "h264" || value == "jpeg"))
            settings.jpeg = value == "jpeg";
        else throw std::invalid_argument("Invalid option or value: " + std::string(option));
    }
    if (!g_hostname_is_ip_address(settings.bind.c_str()) ||
        settings.bind.find(':') != std::string::npos || settings.bind == "0.0.0.0")
        throw std::invalid_argument("--bind requires a specific local IPv4 address");
    return settings;
}

void json(Response& response, const Json& body, int status = 200)
{
    response.status = status;
    response.set_content(body.dump(-1, ' ', false, Json::error_handler_t::replace),
                         "application/json; charset=utf-8");
}

std::string result_text(unsigned result)
{
    static const std::array<const char*, 8> labels{
        "완료", "잘못된 ID", "변경 중", "생성 실패", "이전 모델로 복구",
        "복구 실패", "준비되지 않음", "잘못된 설정"};
    return result < labels.size() ? labels[result] : "Runtime 결과 " + std::to_string(result);
}

class WebApi {
public:
    WebApi(CommandClient& client, VideoReceiver& video, const Settings& settings)
        : client_(client), video_(video), settings_(settings) {}

    void routes(httplib::Server& server, const std::string& page)
    {
        server.Get("/", [&page](const Request&, Response& response) {
            response.set_content(page, "text/html; charset=utf-8");
        });
        server.Get("/model", [this](const Request&, Response& response) {
            json(response, models());
        });
        server.Get("/rules", [this](const Request&, Response& response) {
            json(response, rules());
        });
        server.Post("/model/set", [this](const Request& request, Response& response) {
            change(request, response, false);
        });
        server.Post("/rules/set", [this](const Request& request, Response& response) {
            change(request, response, true);
        });
        server.Post("/gimbal/ping", [this](const Request&, Response& response) {
            client_.ping();
            json(response, {{"running", false}, {"answer", "웹 어댑터에서 짐벌 조작은 미연결입니다"}});
        });
        server.Get("/healthz", [this](const Request&, Response& response) {
            const auto frame = video_.latest();
            json(response, {{"video_live", fresh(frame)}});
        });
        server.Get("/gimbal/people", [this](const Request&, Response& response) {
            const auto frame = video_.latest();
            auto people = Json::array();
            if (fresh(frame)) {
                for (const auto& track : frame->metadata.tracks) {
                    people.push_back({{"id", track.track_id}, {"x1", track.x}, {"y1", track.y},
                        {"x2", track.x + track.width}, {"y2", track.y + track.height},
                        {"label", track.class_id}, {"confidence", track.confidence}});
                }
            }
            json(response, {{"width", kGuiFrameWidth}, {"height", kGuiFrameHeight}, {"people", people}});
        });
        server.Get("/time", [](const Request&, Response& response) {
            const std::chrono::duration<double> epoch = std::chrono::system_clock::now().time_since_epoch();
            json(response, {{"t", epoch.count()}});
        });
        server.Get("/snap.jpg", [this](const Request&, Response& response) {
            const auto frame = video_.latest();
            if (fresh(frame)) response.set_content(frame->jpeg, "image/jpeg");
            else json(response, {{"ok", false}, {"text", "영상 대기 중"}}, 503);
        });
        server.Get("/stream", [this](const Request& request, Response& response) {
            stream(request, response);
        });
    }

    void stop() { stopping_ = true; }

private:
    static bool fresh(const std::shared_ptr<const Frame>& frame)
    {
        return frame && Clock::now() - frame->received < std::chrono::seconds(2);
    }

    Json catalog(bool tracker)
    {
        const auto count_payload = client_.pipeline(tracker ? 2 : 1);
        const auto count = read_u32(count_payload.data());
        Json list = Json::array();
        for (const auto id : tracker ? settings_.tracker_ids : settings_.model_ids) {
            if (list.size() == count) break;
            Bytes id_payload;
            append_u32(id_payload, id);
            const auto reply = client_.pipeline(tracker ? 6 : 5, std::move(id_payload));
            const auto found = read_u32(reply.data());
            if (found != id) continue;
            const auto flags = read_u32(reply.data() + 4);
            const auto name_end = std::find(reply.begin() + 8, reply.end(), 0);
            list.push_back({{"id", found}, {"available", (flags & 1) != 0}, {"active", (flags & 2) != 0},
                            {"name", std::string(reply.begin() + 8, name_end)}});
        }
        return list;
    }

    Json models()
    {
        const auto entries = catalog(false);
        const auto change = client_.change_state(3);
        Json current = nullptr;
        for (const auto& entry : entries)
            if (entry.at("active") == true) current = entry.at("id");
        const bool pending = change && change->phase == 1;
        return {{"catalog", entries}, {"current", current}, {"busy", pending},
                {"pending", pending ? Json(change->target) : Json(nullptr)},
                {"last", last_change(change)}};
    }

    static Json last_change(const std::optional<Change>& change)
    {
        if (!change || change->phase != 2) return nullptr;
        return {{"at", ""}, {"result", change->result}, {"text", result_text(change->result)}};
    }

    Json rules()
    {
        auto presets = Json::array();
        for (const auto& entry : catalog(true)) {
            presets.push_back({{"key", std::to_string(entry.at("id").get<unsigned>())},
                               {"name", entry.at("name")}});
        }
        const auto policy = client_.pipeline(7);
        const auto change = client_.change_state(4);
        const bool pending = change && change->phase == 1;
        return {{"presets", presets}, {"tracker_backend", "C++"},
                {"tracker", std::to_string(read_u32(policy.data()))},
                {"warm_min", read_u32(policy.data() + 4)}, {"warm_max", read_u32(policy.data() + 8)},
                {"edge_birth", read_float(policy.data() + 12)},
                {"confirm", {{"frames", read_u32(policy.data() + 16)}, {"ms", read_u32(policy.data() + 20)}}},
                {"pending", pending ? Json{{"tracker", std::to_string(change->target)}} : Json(nullptr)},
                {"tracker_last", last_change(change)}};
    }

    void change(const Request& request, Response& response, bool tracker)
    {
        const auto* key = tracker ? "tracker" : "id";
        if (!request.has_param(key) ||
            (tracker && request.params.size() == 2 && request.get_param_value("engine") != "auto")) {
            json(response, {{"ok", false}, {"text", "아직 연결되지 않은 기능입니다"}}, 501);
            return;
        }
        if (request.get_param_value_count(key) != 1 ||
            request.params.size() != (request.has_param("engine") && tracker ? 2u : 1u))
            throw std::invalid_argument("잘못된 전환 요청입니다");
        const auto requested = client_.change(tracker ? 4 : 3, number(request.get_param_value(key)));
        auto state = tracker ? rules() : models();
        const auto completed = client_.change_state(tracker ? 4 : 3);
        const auto result = requested.phase == 1 && completed &&
            completed->target == requested.target ? *completed : requested;
        state["ok"] = result.result == 0;
        state["phase"] = result.phase;
        state["result"] = result.result;
        state["text"] = result.phase == 1 ? "전환 요청 수락" : result_text(result.result);
        json(response, state);
    }

    void stream(const Request& request, Response& response)
    {
        // Limit long-lived streams so other browser API calls have workers available.
        if (streams_++ >= 8) {
            --streams_;
            json(response, {{"ok", false}, {"text", "영상 연결 한도 초과"}}, 503);
            return;
        }
        const auto* type = request.get_param_value("f") == "1"
            ? "multipart/mixed; boundary=frame" : "multipart/x-mixed-replace; boundary=frame";
        response.set_chunked_content_provider(type, [this, sequence = std::uint64_t{0}](
            std::size_t, httplib::DataSink& sink) mutable {
            if (stopping_) { sink.done(); return true; }
            const auto frame = video_.next(sequence);
            if (!frame || stopping_) { sink.done(); return true; }
            sequence = frame->sequence;
            const auto header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                std::to_string(frame->jpeg.size()) + "\r\n\r\n";
            return sink.write(header.data(), header.size()) &&
                sink.write(frame->jpeg.data(), frame->jpeg.size()) && sink.write("\r\n", 2);
        }, [this](bool) { --streams_; });
    }

    CommandClient& client_;
    VideoReceiver& video_;
    const Settings& settings_;
    std::atomic_bool stopping_{false};
    std::atomic_uint streams_{0};
};

bool valid_host(const Request& request, const Settings& settings)
{
    const auto host = request.get_header_value("Host");
    const auto suffix = ":" + std::to_string(settings.http_port);
    const auto matches = [&](const std::string& expected) {
        return host == expected + suffix || (settings.http_port == 80 && host == expected);
    };
    return matches(settings.bind) || (settings.bind == "127.0.0.1" && matches("localhost"));
}

int run(const Settings& settings)
{
    std::ifstream html(settings.page, std::ios::binary);
    if (!html) throw std::runtime_error("Cannot read model-bank page: " + settings.page);
    const std::string page{std::istreambuf_iterator<char>(html), {}};
    if (page.empty()) throw std::runtime_error("model-bank page is empty");

    gst_init(nullptr, nullptr);
    CommandClient client(settings.board, settings.command_port);
    VideoReceiver video(settings.video_port, settings.metadata_port, settings.jpeg);
    WebApi api(client, video, settings);
    httplib::Server server;
    server.new_task_queue = [] { return new httplib::ThreadPool(16, 64); };
    server.set_read_timeout(3).set_write_timeout(3);
    server.set_payload_max_length(8192);
    server.set_default_headers({{"Cache-Control", "no-store"}});
    server.set_pre_routing_handler([&settings](const Request& request, Response& response) {
        if (valid_host(request, settings) &&
            (request.method != "POST" || (request.get_header_value("X-Gimbal") == "1" &&
             (!request.has_header("Origin") ||
              request.get_header_value("Origin") == "http://" + request.get_header_value("Host")))))
            return httplib::Server::HandlerResponse::Unhandled;
        json(response, {{"ok", false}, {"text", "요청 주소 또는 출처가 일치하지 않습니다"}}, 403);
        return httplib::Server::HandlerResponse::Handled;
    });
    server.set_exception_handler([](const Request&, Response& response, std::exception_ptr error) {
        try { std::rethrow_exception(error); }
        catch (const std::invalid_argument& e) { json(response, {{"ok", false}, {"text", e.what()}}, 400); }
        catch (const std::exception& e) { json(response, {{"ok", false}, {"text", e.what()}}, 503); }
    });
    server.set_error_handler([](const Request&, Response& response) {
        if (response.status == 404)
            json(response, {{"ok", false}, {"text", "아직 연결되지 않은 기능입니다"}}, 501);
    });
    api.routes(server, page);
    if (!server.bind_to_port(settings.bind, settings.http_port))
        throw std::runtime_error("Cannot bind HTTP server to " + settings.bind);

    auto* loop = g_main_loop_new(nullptr, false);
    const auto stop_loop = [](gpointer data) -> gboolean {
        g_main_loop_quit(static_cast<GMainLoop*>(data));
        return G_SOURCE_CONTINUE;
    };
    const auto interrupt = g_unix_signal_add(SIGINT, stop_loop, loop);
    const auto terminate = g_unix_signal_add(SIGTERM, stop_loop, loop);
    bool listening = false;
    std::thread worker([&] {
        listening = server.listen_after_bind();
        g_idle_add([](gpointer data) -> gboolean {
            g_main_loop_quit(static_cast<GMainLoop*>(data));
            return G_SOURCE_REMOVE;
        }, loop);
    });
    std::cout << "http://" << settings.bind << ':' << settings.http_port << '/' << std::endl;
    g_main_loop_run(loop);
    api.stop();
    server.stop();
    worker.join();
    // Dispatch any pending worker-exit notification before releasing the loop.
    while (g_main_context_iteration(nullptr, false)) {}
    g_source_remove(interrupt);
    g_source_remove(terminate);
    g_main_loop_unref(loop);
    return listening ? 0 : 1;
}
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "lwir_web_gui [--bind IPv4] [--http-port PORT] [--page FILE]\n"
                     "             [--board HOST] [--command-port PORT] [--codec h264|jpeg]\n"
                     "             [--video-port PORT] [--metadata-port PORT]\n"
                     "             [--model-ids ID,...] [--tracker-ids ID,...]\n";
        return 0;
    }
    try {
        return web_gui::run(web_gui::arguments(argc, argv));
    } catch (const std::exception& e) {
        std::cerr << "lwir_web_gui: " << e.what() << '\n';
        return 1;
    }
}
