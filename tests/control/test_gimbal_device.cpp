#include <chrono>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>
#include "control/gimbal_device.hpp"

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fd { int value; ~Fd() { if (value >= 0) ::close(value); } };
std::vector<std::uint8_t> read_bytes(int fd, std::size_t size)
{
    std::vector<std::uint8_t> bytes(size);
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (offset < size) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        check(remaining > 0, "test read overall timeout");
        pollfd p{fd, POLLIN, 0};
        const int ready = ::poll(&p, 1, static_cast<int>(remaining));
        if (ready < 0 && errno == EINTR) continue;
        check(ready > 0, "test read timeout");
        const auto count = ::read(fd, bytes.data() + offset, size - offset);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        check(count > 0, "test read failed");
        offset += count;
    }
    return bytes;
}
void serial_line(int fd, const std::string& expected)
{
    auto bytes = read_bytes(fd, expected.size() + 1);
    check(std::string(bytes.begin(), bytes.end()) == expected + "\n", "incorrect vendor serial line");
}
void serial_deadline()
{
    Fd master{::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK)};
    check(master.value >= 0 && !::grantpt(master.value) && !::unlockpt(master.value), "deadline PTY setup");
    GimbalConfig config;
    config.device = ::ptsname(master.value);
    config.write_timeout_ms = 15;
    auto device = GimbalDeviceFactory::create(config);
    const auto started = std::chrono::steady_clock::now();
    bool failed = false;
    // Do not drain the PTY: a backed-up device must not block the TCP worker forever.
    for (int i = 0; i < 10000; ++i) {
        try { device->move(1, 0); }
        catch (const std::runtime_error&) { failed = true; break; }
    }
    check(failed, "serial backlog did not reach a bounded write failure");
    check(std::chrono::steady_clock::now() - started < std::chrono::seconds(2), "serial deadline was not bounded");
}
void serial_commands() {
    Fd master{::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK)};
    check(master.value >= 0 && !::grantpt(master.value) && !::unlockpt(master.value), "PTY setup");
    GimbalConfig config; config.device = ::ptsname(master.value);
    config.speed = 37; config.acceleration = 61; config.x_sign = -1;
    config.home_x_deg = 12; config.home_y_deg = -5;
    auto device = GimbalDeviceFactory::create(config);
    device->move(-1, 1);
    serial_line(master.value, "{\"T\":141,\"X\":1,\"Y\":1,\"SPD\":37}");
    device->move_with_speed(1,0,12);
    serial_line(master.value, "{\"T\":141,\"X\":-1,\"Y\":0,\"SPD\":12}");
    device->move_with_speed(1,0,100); // AUTO may exceed the manual speed of 37.
    serial_line(master.value, "{\"T\":141,\"X\":-1,\"Y\":0,\"SPD\":100}");
    bool rejected = false;
    try { device->move_with_speed(1,0,101); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "shared command speed ceiling was ignored");
    device->stop();
    serial_line(master.value, "{\"T\":135}");
    device->home(); // encoder contract only; no physical HOME completion assertion
    serial_line(master.value, "{\"T\":133,\"X\":12,\"Y\":-5,\"SPD\":37,\"ACC\":61}");
}
}
int main() try { serial_deadline(); serial_commands(); return 0; }
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
