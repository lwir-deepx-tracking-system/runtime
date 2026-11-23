#include "lan.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>

namespace web_gui {
namespace {
std::runtime_error network_error(GError* error)
{
    std::unique_ptr<GError, decltype(&g_error_free)> owned(error, g_error_free);
    return std::runtime_error(error ? error->message : "Runtime disconnected");
}
}

void append_u32(Bytes& bytes, std::uint32_t value)
{
    for (unsigned shift : {24, 16, 8, 0})
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::uint32_t read_u32(const std::uint8_t* bytes)
{
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value = (value << 8) | bytes[i];
    return value;
}

float read_float(const std::uint8_t* bytes)
{
    const auto bits = read_u32(bytes);
    float result;
    static_assert(sizeof(bits) == sizeof(result));
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

CommandClient::CommandClient(std::string host, unsigned port)
    : host_(std::move(host)), port_(port) {}

CommandClient::~CommandClient() { disconnect(); }

void CommandClient::disconnect()
{
    g_clear_object(&connection_);
    status_.clear();
    for (const auto& entry : pending_) {
        auto& change = changes_.at(entry.second);
        change.phase = 2;
        change.result = static_cast<std::uint8_t>(PipelineResult::NotReady);
    }
    pending_.clear();
}

void CommandClient::read_exact(std::uint8_t* data, std::size_t remaining,
                               Clock::time_point deadline)
{
    auto* socket = g_socket_connection_get_socket(connection_);
    while (remaining != 0) {
        const auto wait = std::chrono::duration_cast<std::chrono::microseconds>(
            deadline - Clock::now()).count();
        GError* error = nullptr;
        if (wait <= 0) throw std::runtime_error("Runtime reply timed out");
        if (!g_socket_condition_timed_wait(socket, G_IO_IN, wait, nullptr, &error))
            throw network_error(error);
        const auto size = g_socket_receive(socket, reinterpret_cast<char*>(data), remaining,
                                           nullptr, &error);
        if (size <= 0) throw network_error(error);
        data += size;
        remaining -= size;
    }
}

GuiCommand CommandClient::exchange(const GuiCommand& command, std::uint32_t id)
{
    try {
        GError* error = nullptr;
        if (!connection_) {
            auto* client = g_socket_client_new();
            g_socket_client_set_timeout(client, 1);
            g_socket_client_set_enable_proxy(client, false);
            connection_ = g_socket_client_connect_to_host(client, host_.c_str(), port_,
                                                          nullptr, &error);
            g_object_unref(client);
            if (!connection_) throw network_error(error);
        }

        const auto bytes = codec_.encode_command(command);
        if (bytes.empty()) throw std::logic_error("Invalid command payload");
        auto* stream = g_io_stream_get_output_stream(G_IO_STREAM(connection_));
        if (!g_output_stream_write_all(stream, bytes.data(), bytes.size(), nullptr, nullptr, &error))
            throw network_error(error);

        if (command.category == GuiCategory::Gimbal && command.command == 1)
            return {};
        const auto deadline = Clock::now() + std::chrono::milliseconds(600);
        for (;;) {
            Bytes packet(kGuiCommandHeaderSize);
            read_exact(packet.data(), packet.size(), deadline);
            const auto total = codec_.command_packet_size(packet.data(), packet.size());
            if (!total) throw std::runtime_error("Invalid Runtime command header");
            packet.resize(total);
            read_exact(packet.data() + kGuiCommandHeaderSize, total - kGuiCommandHeaderSize, deadline);
            GuiCommand reply;
            if (!codec_.decode_command(packet.data(), packet.size(), reply))
                throw std::runtime_error("Invalid Runtime reply");
            const auto& body = reply.payload;
            if (reply.category == GuiCategory::System &&
                (reply.command == 2 || reply.command == 3)) status_ = body;
            if (reply.category == GuiCategory::Recording && command.category == reply.category &&
                reply.command == command.command + 128 && read_u32(body.data()) == id) return reply;
            if (command.category == GuiCategory::Gimbal &&
                command.command == static_cast<std::uint8_t>(GuiGimbalCommand::Pose) &&
                reply.category == command.category && reply.command == 0x85) return reply;
            if (reply.category == GuiCategory::Pipeline && body.size() >= 4) {
                const auto request = read_u32(body.data());
                const auto pending = pending_.find(request);
                if (pending != pending_.end() && reply.command == pending->second + 128 &&
                    body.size() == 6 && body[4] == 2) {
                    auto& change = changes_.at(pending->second);
                    change.phase = body[4];
                    change.result = body[5];
                    pending_.erase(pending);
                }
                if (command.category == reply.category && reply.command == command.command + 128 &&
                    request == id) return reply;
            }
            if (reply.category == GuiCategory::System &&
                reply.command == 1 && body.size() == 3 &&
                body[0] == static_cast<std::uint8_t>(command.category) &&
                body[1] == command.command) return reply;
        }
    } catch (...) {
        disconnect();
        throw;
    }
}

Bytes CommandClient::pipeline(std::uint8_t code, Bytes args)
{
    std::lock_guard lock(mutex_);
    if (++id_ == 0) ++id_;
    Bytes payload;
    append_u32(payload, id_);
    payload.insert(payload.end(), args.begin(), args.end());
    auto reply = exchange({GuiCategory::Pipeline, code, std::move(payload)}, id_);
    return {reply.payload.begin() + 4, reply.payload.end()};
}

Change CommandClient::change(std::uint8_t code, std::uint32_t target)
{
    Bytes args;
    append_u32(args, target);
    return change_policy(code, std::move(args), target);
}

Change CommandClient::change_policy(std::uint8_t code, Bytes args, std::uint32_t target)
{
    std::lock_guard lock(mutex_);
    if (++id_ == 0) ++id_;
    Bytes payload;
    append_u32(payload, id_);
    payload.insert(payload.end(), args.begin(), args.end());
    const auto reply = exchange({GuiCategory::Pipeline, code, std::move(payload)}, id_);
    const Change change{target, reply.payload[4], reply.payload[5]};
    if (change.phase == 1 && change.result == 0) {
        pending_[id_] = code;
        changes_[code] = change;
    } else if (!changes_.count(code) || changes_.at(code).phase != 1) {
        changes_[code] = change;
    }
    return change;
}

std::optional<Change> CommandClient::change_state(std::uint8_t code)
{
    std::lock_guard lock(mutex_);
    const auto found = changes_.find(code);
    if (found == changes_.end()) return std::nullopt;
    return found->second;
}

Bytes CommandClient::recording(std::uint8_t code, Bytes args)
{
    std::lock_guard lock(mutex_);
    if (++id_ == 0) ++id_;
    Bytes payload;
    append_u32(payload, id_);
    payload.insert(payload.end(), args.begin(), args.end());
    auto reply = exchange({GuiCategory::Recording, code, std::move(payload)}, id_);
    return {reply.payload.begin() + 4, reply.payload.end()};
}

std::uint8_t CommandClient::control(GuiCategory category, std::uint8_t code, Bytes args)
{
    std::lock_guard lock(mutex_);
    const auto reply = exchange({category, code, std::move(args)}, 0);
    return reply.payload.empty() ? 0 : reply.payload[2];
}

Bytes CommandClient::pose()
{
    std::lock_guard lock(mutex_);
    return exchange({GuiCategory::Gimbal, static_cast<std::uint8_t>(GuiGimbalCommand::Pose), {}}, 0).payload;
}

Bytes CommandClient::status()
{
    std::lock_guard lock(mutex_);
    return status_;
}

void CommandClient::ping()
{
    std::lock_guard lock(mutex_);
    exchange({GuiCategory::System, 4, {}}, 0);
}
}
