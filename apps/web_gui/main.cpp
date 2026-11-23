#include "replay.hpp"

#include <glib-unix.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <csignal>
#include <cmath>
#include <cstring>
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
    bool gimbal = false;
    std::string recordings = "/mnt/lwir_data/results/runtime/recordings";
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
        else if (option == "--recordings") settings.recordings = value;
        else if (option == "--gimbal" && (value == "off" || value == "on"))
            settings.gimbal = value == "on";
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
        : client_(client), video_(video), settings_(settings), replay_(settings.recordings) {}

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
            if (request.has_param("tracker")) change(request, response, true);
            else change_rules(request, response);
        });
        server.Post("/gimbal/(ping|move|stop|home|click|click-frame)",
            [this](const Request& request, Response& response) { gimbal(request, response); });
        server.Get("/replay", [this](const Request&, Response& response) {
            auto list = replay_.listing(recording_names());
            json(response, {{"available", !list.empty()}, {"list", list}, {"state", replay_.state()},
                            {"eval", replay_.evaluation()},
                            {"backends", Json::array({{{"id", "stored"}, {"label", "저장된 C++ 추적 결과"}}})}});
        });
        server.Get("/replay/eval", [this](const Request& request, Response& response) {
            const bool final = request.get_param_value("view") == "final";
            const auto prefix = request.has_param("pos")
                ? std::optional<unsigned>(integer(request, "pos", 250000)) : std::nullopt;
            auto result = replay_.evaluation(prefix, final);
            json(response, final || prefix ? Json{{"eval", result}} : result);
        });
        server.Post("/replay/(start|stop|seek|pause|play|step|speed|virtual-click|virtual-zoom|eval/draw|eval/reset)",
            [this](const Request& request, Response& response) { replay_command(request, response); });
        server.Post("/system/npu-feed", [this](const Request& request, Response& response) {
            std::lock_guard lock(control_mutex_);
            if (replay_.active()) {
                json(response, {{"ok", false}, {"text", "녹화 재생 중입니다"}}, 409);
                return;
            }
            const auto requested = request.get_param_value("mode");
            unsigned mode = 0;
            if (requested == "arrival") mode = 1;
            else if (requested == "auto") mode = 2;
            else if (requested != "normal")
                throw std::invalid_argument("단독, 교대, 자동 중에서 선택하세요");
            Bytes args;
            append_u32(args, mode);
            const auto code = client_.pipeline(
                static_cast<std::uint8_t>(GuiPipelineCommand::ChangeNpuFeed), std::move(args));
            json(response, {{"ok", code[1] == 0}, {"text", result_text(code[1])}});
        });
        server.Post("/camera/reset", [this](const Request&, Response& response) {
            std::lock_guard lock(control_mutex_);
            if (replay_.active()) {
                json(response, {{"ok", false}, {"text", "녹화 재생 중입니다"}}, 409);
                return;
            }
            if (settings_.gimbal) halt();
            controller_.clear();
            const auto code = client_.control(GuiCategory::System,
                static_cast<std::uint8_t>(GuiSystem::CameraReopen));
            json(response, {{"ok", code == 0},
                {"text", code ? "카메라 복구 요청을 접수할 수 없습니다" : "카메라 복구 요청 접수"}});
        });
        server.Get("/measure", [this](const Request&, Response& response) {
            json(response, measurement(client_.recording(1)));
        });
        server.Post("/measure/(start|stop)", [this](const Request& request, Response& response) {
            if (request.matches[1] == "stop") {
                json(response, measurement(client_.recording(3)));
                return;
            }
            const auto data = Json::parse(request.body, nullptr, false);
            if (!data.is_object() || !data.value("video", true)) {
                json(response, {{"ok", false}, {"text", "원본 영상 녹화만 지원합니다"}}, 400);
                return;
            }
            const auto name = data.value("name", std::string{});
            if (name.size() > 63 || name.find('\0') != std::string::npos)
                throw std::invalid_argument("녹화 이름이 너무 길거나 올바르지 않습니다");
            Bytes arguments{1};
            arguments.insert(arguments.end(), name.begin(), name.end());
            arguments.resize(65);
            json(response, measurement(client_.recording(2, std::move(arguments))));
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
            const auto replay = replay_.picture();
            const auto frame = replay ? replay : video_.latest();
            if (fresh(frame) || replay) response.set_content(frame->jpeg, "image/jpeg");
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

    static std::uint64_t u64(const std::uint8_t* value)
    {
        return (std::uint64_t(read_u32(value)) << 32) | read_u32(value + 4);
    }

    static Json measurement(const Bytes& status)
    {
        const auto session = read_u32(status.data() + 4);
        const auto frames = u64(status.data() + 8);
        return {{"ok", status[0] == 0}, {"on", status[1] != 0}, {"failed", status[2] != 0},
                {"cur", {{"n", session}}}, {"frames", frames}, {"seconds", 0},
                {"video_frames", frames}, {"video_dropped", u64(status.data() + 16)},
                {"text", status[0] ? "Runtime 녹화 명령 실패" : "완료"}};
    }

    static unsigned integer(const Request& request, const char* key, unsigned maximum)
    {
        const auto value = request.get_param_value(key);
        const auto result = value == "0" ? 0 : number(value);
        if (result > maximum) throw std::invalid_argument("값이 설정 범위를 벗어났습니다");
        return result;
    }

    static double decimal(const Request& request, const char* key, double low, double high)
    {
        const auto value = request.get_param_value(key);
        std::size_t used;
        const auto result = std::stod(value, &used);
        if (used != value.size() || !std::isfinite(result) || result < low || result > high)
            throw std::invalid_argument("값이 설정 범위를 벗어났습니다");
        return result;
    }

    std::vector<std::string> recording_names()
    {
        const auto files = client_.recording(6);
        std::vector<std::string> names;
        if (files[0]) return names;
        for (auto begin = files.begin() + 1; begin != files.end(); begin += 64)
            names.emplace_back(begin, std::find(begin, begin + 64, 0));
        return names;
    }

    void halt()
    {
        client_.control(GuiCategory::Gimbal, 1, {0, 0});
        if (client_.control(GuiCategory::Mode, 2))
            throw std::runtime_error("Runtime에서 정지를 확인하지 못했습니다");
    }

    void replay_command(const Request& request, Response& response)
    {
        std::lock_guard lock(control_mutex_);
        const auto command = request.matches[1].str();
        if (command == "start") {
            const auto name = request.get_param_value("seq");
            const auto names = recording_names();
            if (std::find(names.begin(), names.end(), name) == names.end() ||
                request.get_param_value("backend") != "stored")
                throw std::invalid_argument("완료된 녹화와 저장 결과 재생을 선택하세요");
            const auto fps = integer(request, "fps", 30);
            if (fps < 5) throw std::invalid_argument("재생 FPS는 5~30입니다");
            if (settings_.gimbal) halt();
            controller_.clear();
            replay_.start(name, fps, request.get_param_value("loop") == "1");
        } else if (command == "stop") replay_.stop();
        else if (command == "pause" || command == "play") replay_.play(command == "play");
        else if (command == "step") replay_.step();
        else if (command == "seek") replay_.seek(integer(request, "pos", 250000));
        else if (command == "speed") {
            const auto fps = integer(request, "fps", 30);
            if (fps < 5) throw std::invalid_argument("재생 FPS는 5~30입니다");
            replay_.speed(fps);
        } else if (command == "eval/reset") replay_.restart();
        else if (command == "eval/draw") replay_.draw_truth(integer(request, "on", 1));
        else if (command == "virtual-click")
            replay_.select(decimal(request, "x", 0, 1) * kGuiFrameWidth,
                           decimal(request, "y", 0, 1) * kGuiFrameHeight);
        else if (command == "virtual-zoom") replay_.zoom(decimal(request, "zoom", 1, 5));
        json(response, {{"ok", true}});
    }

    void gimbal(const Request& request, Response& response)
    {
        std::lock_guard lock(control_mutex_);
        const auto command = request.matches[1].str();
        const auto browser = request.get_header_value("X-LWIR-Control");
        const auto now = Clock::now();
        if (!controller_.empty() && now - controller_seen_ >= std::chrono::seconds(1)) {
            halt();
            controller_.clear();
        }
        if (command != "ping" && (!settings_.gimbal || replay_.active() ||
            browser.empty() || browser.size() > 32)) {
            json(response, {{"running", false}, {"answer", "현재 짐벌 조작을 사용할 수 없습니다"}}, 409);
            return;
        }
        if (!controller_.empty() && controller_ != browser) {
            json(response, {{"running", false}, {"answer", "다른 브라우저에서 제어 중입니다"}}, 409);
            return;
        }
        if (command == "ping") {
            client_.ping();
            if (!controller_.empty()) controller_seen_ = now;
        }
        std::uint8_t result = 0;
        std::string answer;
        if (command == "move") {
            const auto direction = [&](const char* key) -> std::uint8_t {
                const auto value = request.get_param_value(key);
                if (value == "-1") return 255;
                if (value == "0") return 0;
                if (value == "1") return 1;
                throw std::invalid_argument("잘못된 이동 방향입니다");
            };
            client_.control(GuiCategory::Gimbal, 1, {direction("x"), direction("y")});
        } else if (command == "stop") halt();
        else if (command == "home") result = client_.control(GuiCategory::Gimbal, 3);
        else if (command == "click" || command == "click-frame") {
            const auto value = request.get_param_value("frame");
            std::uint64_t id = 0;
            if (!value.empty()) {
                const auto parsed = std::from_chars(value.data(), value.data()+value.size(), id);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size()) id = 0;
            }
            const auto frame = video_.shown_metadata(id);
            if (!frame) {
                json(response, {{"running", true}, {"answer", "표시된 추적 프레임이 갱신됐습니다"}}, 409);
                return;
            }
            const auto x = decimal(request, "x", 0, 1) * kGuiFrameWidth;
            const auto y = decimal(request, "y", 0, 1) * kGuiFrameHeight;
            const auto& tracks = frame->tracks;
            const auto target = std::find_if(tracks.begin(), tracks.end(), [&](const auto& t) {
                return t.class_id == 0 && x >= t.x && x <= t.x+t.width && y >= t.y && y <= t.y+t.height;
            });
            if (target == tracks.end()) halt();
            else {
                Bytes id;
                append_u32(id, target->track_id);
                result = client_.control(GuiCategory::Mode, 1, std::move(id));
            }
        }
        if (command != "ping" && !result) {
            controller_ = browser;
            controller_seen_ = now;
        }
        if (command == "stop") controller_.clear();
        if (result) answer = "Runtime에서 명령을 승인하지 않았습니다";
        if (!settings_.gimbal) answer = "웹 짐벌 조작이 비활성입니다";
        const auto state = client_.status();
        const bool valid = state.size() == 8;
        Json summary{{"running", settings_.gimbal && valid && !replay_.active()}, {"ok", !result},
                        {"answer", answer}, {"auto", valid && state[0] != 0}, {"lost", valid && state[1] == 2},
                        {"moving", valid && (state[2] == 1 || state[2] == 2)},
                        {"error", valid && state[3] != 0}, {"id", valid ? read_u32(state.data() + 4) : 0}};
        if (command == "ping" && settings_.gimbal && !replay_.active()) {
            const auto pose = client_.pose();
            if (pose[0]) summary["pose"] = {
                {"pan", {{"deg", read_float(pose.data()+1)}}},
                {"tilt", {{"deg", read_float(pose.data()+5)}}}};
        }
        json(response, summary);
    }

    void change_rules(const Request& request, Response& response)
    {
        std::uint8_t command = 8;
        Bytes payload;
        const auto has = [&](const char* name) { return request.has_param(name); };
        if (has("auto_det") && request.params.size() == 1) {
            command = 12;
            append_u32(payload, integer(request, "auto_det", 1));
        } else if (has("auto_far_px") && has("auto_near_px") && request.params.size() == 2) {
            command = 13;
            for (const auto* name : {"auto_far_px", "auto_near_px"}) {
                const float value = decimal(request, name, 1, 480);
                std::uint32_t bits;
                std::memcpy(&bits, &value, sizeof(bits));
                append_u32(payload, bits);
            }
        } else if (has("predictor") || has("box_refine") || has("lost_same") || has("box_aim_y")) {
            command = 10;
            const unsigned mask = (has("predictor") ? 1u : 0u) | (has("box_refine") ? 2u : 0u) |
                (has("lost_same") ? 4u : 0u) | (has("box_aim_y") ? 8u : 0u);
            if (request.params.size() != unsigned(has("predictor")) + unsigned(has("box_refine")) +
                                         unsigned(has("lost_same")) + unsigned(has("box_aim_y")))
                throw std::invalid_argument("지원하지 않는 설정 조합입니다");
            append_u32(payload, mask);
            const auto select = [&](const char* key, std::initializer_list<std::string_view> choices) {
                if (!has(key)) return 0u;
                const auto input = request.get_param_value(key);
                const auto found = std::find(choices.begin(), choices.end(), input);
                if (found == choices.end()) throw std::invalid_argument("지원하지 않는 설정값입니다");
                return static_cast<unsigned>(found - choices.begin());
            };
            append_u32(payload, select("predictor", {"kalman", "gru_kpt", "mamba_kpt", "gru_near_kpt"}));
            append_u32(payload, select("box_refine", {"off", "size", "body"}));
            append_u32(payload, has("lost_same") ? integer(request, "lost_same", 1) : 0);
            const float aim = has("box_aim_y") ? decimal(request, "box_aim_y", 0, 1) : .5f;
            std::uint32_t bits;
            std::memcpy(&bits, &aim, sizeof(bits));
            append_u32(payload, bits);
        } else {
            const std::array<const char*, 6> fields{"warm_min", "warm_max", "edge_birth",
                                                    "confirm_ms", "gmc", "class_gate"};
            unsigned mask = 0, count = 0;
            for (unsigned i = 0; i < fields.size(); ++i)
                if (has(fields[i])) { mask |= 1u << i; ++count; }
            if (!mask || request.params.size() != count) {
                json(response, {{"ok", false}, {"text", "아직 연결되지 않은 설정입니다"}}, 501);
                return;
            }
            append_u32(payload, mask);
            append_u32(payload, has("warm_min") ? integer(request, "warm_min", 65535) : 0);
            append_u32(payload, has("warm_max") ? integer(request, "warm_max", 65535) : 0);
            const float edge = has("edge_birth") ? decimal(request, "edge_birth", 0, 1) : 0;
            std::uint32_t bits;
            std::memcpy(&bits, &edge, sizeof(bits));
            append_u32(payload, bits);
            append_u32(payload, has("confirm_ms") ? integer(request, "confirm_ms", 1000) : 0);
            append_u32(payload, (has("gmc") ? integer(request, "gmc", 1) : 0) |
                                (has("class_gate") ? integer(request, "class_gate", 1) << 1 : 0));
        }
        const auto accepted = client_.change_policy(command, std::move(payload));
        auto state = rules();
        const auto completed = client_.change_state(command);
        const auto result = accepted.phase == 1 && completed ? *completed : accepted;
        state["ok"] = result.result == 0;
        state["phase"] = result.phase;
        state["text"] = result.phase == 1 ? "설정 요청 수락" : result_text(result.result);
        json(response, state);
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
        Json out{{"presets", presets}, {"tracker_backend", "C++"},
                {"tracker", std::to_string(read_u32(policy.data()))},
                {"warm_min", read_u32(policy.data() + 4)}, {"warm_max", read_u32(policy.data() + 8)},
                {"edge_birth", read_float(policy.data() + 12)},
                {"confirm", {{"frames", read_u32(policy.data() + 16)}, {"ms", read_u32(policy.data() + 20)}}},
                {"pending", pending ? Json{{"tracker", std::to_string(change->target)}} : Json(nullptr)},
                {"tracker_last", last_change(change)}};
        static const std::array<std::string_view, 4> predictors{"kalman", "gru_kpt", "mamba_kpt", "gru_near_kpt"};
        static const std::array<std::string_view, 3> refinement{"off", "size", "body"};
        const auto features = client_.pipeline(9);
        const auto predictor = std::min<std::size_t>(read_u32(features.data()), predictors.size()-1);
        const auto mode = std::min<std::size_t>(read_u32(features.data()+4), refinement.size()-1);
        out["predictor"] = predictors[predictor];
        out["predictors"] = Json::array();
        for (const auto option : predictors) out["predictors"].push_back({{"key", option}});
        out["box_refine"] = {{"mode", refinement[mode]}, {"lost_same", read_u32(features.data()+8) != 0},
                             {"modes", Json::array({{{"key", "body"}, {"available", true}}})}};
        for (const auto command : {8, 10}) {
            const auto update = client_.change_state(command);
            if (update) out["policy_last"] = last_change(update);
        }
        const auto feed = client_.pipeline(static_cast<std::uint8_t>(GuiPipelineCommand::NpuFeed));
        const auto feed_flags = read_u32(feed.data());
        const auto requested = read_u32(feed.data()+4);
        const bool alternating = (feed_flags & 2) != 0;
        out["npu_feed"] = {{"can_toggle", (feed_flags & 1) != 0}, {"active", alternating},
                           {"mode", alternating ? "arrival" : "normal"},
                           {"requested", requested == 2 ? "auto" : requested == 1 ? "arrival" : "normal"},
                           {"tile_available", false}, {"cores", {read_u32(feed.data()+8), read_u32(feed.data()+12)}}};
        const auto automatic = client_.pipeline(11);
        const auto enabled = read_u32(automatic.data()) != 0;
        const auto available = read_u32(automatic.data() + 4) != 0;
        out["auto_det"] = {{"on", enabled}, {"mode", enabled ? "live" : "off"},
                           {"can_switch", available},
                           {"near", {{"present", available}, {"id", read_u32(automatic.data()+8)}}},
                           {"far", {{"present", available}, {"id", read_u32(automatic.data()+12)}}},
                           {"params", {{"far_px", read_float(automatic.data()+16)},
                                       {"near_px", read_float(automatic.data()+20)}}}};
        return out;
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
        response.set_chunked_content_provider(type, [this, sequence = std::uint64_t{0},
                                                       replay_sequence = std::uint64_t{0}](
            std::size_t, httplib::DataSink& sink) mutable {
            if (stopping_) { sink.done(); return true; }
            std::shared_ptr<const Frame> frame;
            try {
                frame = replay_.picture();
                const auto deadline = Clock::now() + std::chrono::milliseconds(400);
                while (frame && frame->sequence == replay_sequence && !stopping_ &&
                       sink.is_writable() && Clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    frame = replay_.picture();
                }
            } catch (const std::exception&) {
                sink.done();
                return true;
            }
            if (frame) replay_sequence = frame->sequence;
            else {
                frame = video_.next(sequence);
                if (frame) sequence = frame->sequence;
            }
            if (!frame || stopping_ || !sink.is_writable()) { sink.done(); return true; }
            const auto header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                std::to_string(frame->jpeg.size()) + "\r\n\r\n";
            return sink.write(header.data(), header.size()) &&
                sink.write(frame->jpeg.data(), frame->jpeg.size()) && sink.write("\r\n", 2);
        }, [this](bool) { --streams_; });
    }

    CommandClient& client_;
    VideoReceiver& video_;
    const Settings& settings_;
    Replay replay_;
    std::mutex control_mutex_;
    std::string controller_;
    Clock::time_point controller_seen_;
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
    std::string page{std::istreambuf_iterator<char>(html), {}};
    if (settings.gimbal) {
        const std::string click = "post('/gimbal/click?x='+x+'&y='+y)";
        const auto at = page.find(click);
        if (at == std::string::npos)
            throw std::runtime_error("model-bank page click contract changed");
        page.replace(at, click.size(),
            "post('/gimbal/click-frame?x='+x+'&y='+y+'&frame='+(window.lwirFrame||''))");
        const std::string shown = "pictureShown(t,i.l||0);";
        const auto displayed = page.find(shown);
        if (displayed == std::string::npos)
            throw std::runtime_error("model-bank frame callback changed");
        page.insert(displayed, "window.lwirFrame=Number.isSafeInteger(i.k)?i.k:0;");
        for (const std::string hook : {"headers:{'X-Gimbal':'1'},cache:'no-store',keepalive:",
                                      "headers:{'X-Gimbal':'1'},cache:'no-store',signal:c.signal"}) {
            const auto first = page.find(hook);
            if (first == std::string::npos)
                throw std::runtime_error("model-bank control header contract changed");
            page.insert(first + std::string("headers:{'X-Gimbal':'1'").size(),
                        ",'X-LWIR-Control':pageId");
        }
    }
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
                     "             [--model-ids ID,...] [--tracker-ids ID,...]\n"
                     "             [--recordings DIRECTORY] [--gimbal on|off]\n";
        return 0;
    }
    try {
        return web_gui::run(web_gui::arguments(argc, argv));
    } catch (const std::exception& e) {
        std::cerr << "lwir_web_gui: " << e.what() << '\n';
        return 1;
    }
}
