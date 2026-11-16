#include "runtime/encoder_cpu_monitor.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
GuiVideoStats counts(std::uint64_t submitted, std::uint64_t encoded)
{
    return {submitted, encoded, 0, 0, 0, 0};
}
}

int main()
try
{
    EncoderCpuMonitor monitor;
    require(!monitor.poll(0, 0, counts(0, 0)), "idle must not start the window");
    require(!monitor.poll(100, 200, counts(100, 100)), "first output starts warmup");
    require(!monitor.poll(109, 270, counts(370, 370)), "warmup is excluded");
    require(!monitor.poll(110, 300, counts(400, 400)), "warmup establishes baseline");
    require(!monitor.poll(110.5, 301, counts(415, 415)), "sample interval is at least 1s");
    auto a = monitor.poll(111, 301.5, counts(430, 429));
    require(a && near(a->cpu_percent_one_core, 150), "all process cores may exceed 100%");
    require(near(a->average_cpu_percent_one_core, 150), "startup CPU must be excluded");
    require(near(a->submitted_fps, 30) && near(a->tx_fps, 29), "submitted and output rates differ");
    require(!a->final, "do not finish early");
    auto b = monitor.poll(113, 302.5, counts(490, 489));
    require(b && near(b->cpu_percent_one_core, 50), "use actual delayed poll interval");
    require(near(b->average_cpu_percent_one_core, 100 * 2.5 / 3), "average is time-weighted");
    require(near(b->average_tx_fps, 89.0 / 3), "output average uses whole measurement window");
    auto end = monitor.poll(170.25, 360.25, counts(2207, 2200));
    require(end && end->final && near(end->elapsed_seconds, 60.25), "finish on actual elapsed time");
    require(near(end->average_cpu_percent_one_core, 100), "summary CPU uses measured duration");
    require(end->submitted == 1807 && end->encoded == 1800, "summary excludes warmup counters");
    require(!monitor.poll(172, 362, counts(2260, 2250)), "one window per run");

    EncoderCpuMonitor restarted;
    restarted.poll(0, 0, counts(1, 1));
    restarted.poll(10, 10, counts(301, 301));
    require(!restarted.poll(11, 11, counts(1, 1)), "sender reset must not underflow counters");
    require(!restarted.poll(12, 12, counts(31, 31)), "restart requires a new warmup");
    require(!restarted.poll(22, 22, counts(331, 331)), "new baseline");
    auto after = restarted.poll(23, 23, counts(361, 361));
    require(after && after->encoded == 30, "new window must exclude old session");
    return 0;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << '\n';
    return 1;
}
