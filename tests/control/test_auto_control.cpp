#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "control/gimbal_command_executor.hpp"
#include "control/tracking_command_handler.hpp"
#include "pipeline/control_thread.hpp"

using namespace std::chrono_literals;
namespace {
void check(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
template<class F> void until(F f, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!f()) {
        check(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(2ms);
    }
}
Track track(float cx = 480, float cy = 240) {
    Track t;
    t.track_id = 7; t.x = cx - 10; t.y = cy - 10; t.width = t.height = 20;
    t.confidence = 0.9;
    return t;
}
// Explicit control fixtures, not inference outputs or hardware measurements.
std::shared_ptr<TrackingResult> frame(std::uint64_t id, bool found = true) {
    auto f = std::make_shared<FrameContext>();
    f->metadata = {id, std::chrono::steady_clock::now()};
    f->image = cv::Mat::zeros(480, 640, CV_16UC1);
    auto r = std::make_shared<TrackingResult>(); r->frame = f;
    if (found) r->tracks.push_back(track());
    return r;
}
struct Event { int x, y, speed; bool stop; };
struct Recording {
    std::mutex mutex;
    std::vector<Event> events;
    std::thread::id owner{};
    bool multiple_owners = false;
    std::atomic<bool> fail{false};
    void record(Event event) {
        std::lock_guard<std::mutex> lock(mutex);
        if (owner == std::thread::id{}) owner = std::this_thread::get_id();
        multiple_owners |= owner != std::this_thread::get_id();
        events.push_back(event);
    }
    std::vector<Event> read() { std::lock_guard<std::mutex> lock(mutex); return events; }
};
class RecordingDevice final : public IGimbalDevice {
    std::shared_ptr<Recording> log_;
public:
    explicit RecordingDevice(std::shared_ptr<Recording> log) : log_(std::move(log)) {}
    void move(int x, int y) override { move_with_speed(x, y, 30); }
    void move_with_speed(int x, int y, int speed) override {
        if (log_->fail.load()) throw std::runtime_error("explicit serial failure fixture");
        log_->record({x, y, speed, false});
    }
    void stop() override { log_->record({0, 0, 0, true}); }
    void home() override { throw std::runtime_error("Unexpected HOME"); }
};
ControlConfig config() {
    ControlConfig c; c.enabled = true; c.heartbeat_timeout_ms = 1000;
    c.automatic.period_ms = 5; c.automatic.input_timeout_ms = 100;
    c.automatic.reacquire_timeout_ms = 180;
    return c;
}
void calculations() {
    AutoControlConfig c; c.kp = 100; c.deadband_x = .25; c.deadband_y = .05;
    AutoTrackingController controller(c);
    auto d = controller.calculate(track(384, 264), 640, 480); // .20 inside x, .10 outside y
    check(d.x == 0 && d.y == -1 && std::abs(d.speed - 10) < .001, "Inactive axis inflated common SPD");
    check(controller.calculate(track(320, 240), 640, 480).stop(), "center did not STOP");
    d = controller.calculate(track(160, 120), 640, 480);
    check(d.x == -1 && d.y == 1 && d.speed == c.speed_max, "direction/saturation");
    // PC ManualInputHandler defines +X=Right and +Y=Up. AUTO must match that
    // contract; checking sign(error_y) alone would reproduce the original bug.
    AutoTrackingController directions(AutoControlConfig{});
    check(directions.calculate(track(320,120),640,480).y == 1, "above-center target must command UP (+Y)");
    check(directions.calculate(track(320,360),640,480).y == -1, "below-center target must command DOWN (-Y)");
    check(directions.calculate(track(160,240),640,480).x == -1, "left target must command LEFT");
    check(directions.calculate(track(480,240),640,480).x == 1, "right target must command RIGHT");
    auto bad = track(); bad.width = 0;
    check(!controller.valid(bad,640,480), "zero width");
    bad = track(); bad.x = std::numeric_limits<float>::quiet_NaN();
    check(!controller.valid(bad,640,480), "NaN box");
    bad = track(); bad.predicted = true;
    check(!controller.valid(bad,640,480), "predicted box used for AUTO");
    bad = track(); bad.confidence = .1;
    check(!controller.valid(bad,640,480), "low confidence");
    check(!controller.valid(track(650),640,480), "off-frame center");
    c.fov_x_deg = c.fov_y_deg = 90; c.deadband_x = c.deadband_y = 0;
    d = AutoTrackingController(c).calculate(track(480),640,480);
    check(std::abs(d.error_x - std::atan(.5)) < 1e-6, "FOV radians");
    c.kp = .01;
    check(AutoTrackingController(c).calculate(track(480),640,480).speed == c.speed_min, "moving minimum speed");
    GimbalConfig g; g.x_sign = -1; g.speed = 30;
    check(WaveshareCommands::move(g, 1, 0, 12) == "{\"T\":141,\"X\":-1,\"Y\":0,\"SPD\":12}", "variable SPD/sign encoding");
    bool rejected = false;
    try { WaveshareCommands::move(g,1,0,31); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "out of range SPD accepted");
}
void yaml_contract() {
    auto yaml = YAML::Load("enabled: true\ngimbal: {speed: 10}\nauto: {speed_max: 10}\n");
    check(config_parser::load_control_config(yaml).automatic.speed_max == 10, "AUTO config parse");
    auto rejects = [&] {
        try { config_parser::load_control_config(yaml); return false; }
        catch (const std::runtime_error&) { return true; }
    };
    yaml["auto"]["speed_max"] = 11; check(rejects(), "SPD cap config");
    yaml["auto"]["speed_max"] = 10;
    yaml["auto"]["kp"] = std::numeric_limits<double>::quiet_NaN(); check(rejects(), "nonfinite gain");
    yaml["auto"]["kp"] = 30;
    yaml["auto"]["fov_x_deg"] = 45; check(rejects(), "partial FOV accepted");
    yaml["auto"]["fov_y_deg"] = 45;
    yaml["auto"]["input_timeout_ms"] = 1; check(rejects(), "timeout below period");
}
void startup_failure_and_independent_poll() {
    class StopFailingDevice final : public IGimbalDevice {
        void move(int,int) override {}
        void stop() override { throw std::runtime_error("explicit startup STOP failure fixture"); }
        void home() override {}
    };
    RuntimeControl runtime; auto c = config();
    bool rejected = false;
    try { GimbalCommandExecutor bad(c, std::make_unique<StopFailingDevice>(), runtime); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "startup STOP failure did not abort construction");
    c.heartbeat_timeout_ms = 100; c.poll_interval_ms = 5;
    c.automatic.period_ms = 1000; c.automatic.input_timeout_ms = 2000;
    c.automatic.reacquire_timeout_ms = 3000;
    auto log = std::make_shared<Recording>();
    GimbalCommandExecutor e(c,std::make_unique<RecordingDevice>(log),runtime);
    e.connected(); e.move(1,0);
    const auto start = std::chrono::steady_clock::now();
    until([&] { return !e.snapshot().moving; }, "slow AUTO period blocked heartbeat watchdog");
    check(std::chrono::steady_clock::now() - start < 500ms, "watchdog waited for AUTO period");
    check(log->read().back().stop, "independent heartbeat poll did not STOP");
}
void state_and_watchdog() {
    auto c = config(); RuntimeControl runtime;
    auto log = std::make_shared<Recording>();
    GimbalCommandExecutor executor(c, std::make_unique<RecordingDevice>(log), runtime);
    executor.connected();
    check(executor.select(0) == ControlResult::InvalidTarget, "zero selection");
    check(executor.select(0xffffffffU) == ControlResult::InvalidTarget, "uint32 truncation");
    executor.publish(frame(1));
    check(executor.select(7) == ControlResult::Ok, "AUTO rejected fresh target");
    until([&] { return executor.snapshot().moving; }, "AUTO did not move");
    // No further frames: independent watchdog must STOP and wait for reacquisition.
    until([&] { auto s = executor.snapshot(); return s.lost && !s.moving; }, "no-frame watchdog STOP");
    check(log->read().back().stop, "watchdog ramped down instead of STOP");
    executor.publish(frame(2));
    until([&] { auto s = executor.snapshot(); return s.moving && !s.lost; }, "same-ID reacquisition");
    executor.publish(frame(3, false));
    until([&] { return executor.snapshot().lost; }, "missing Track did not STOP");
    executor.ping();
    until([&] { return !executor.snapshot().automatic; }, "reacquire expiry did not clear AUTO");
    check(executor.snapshot().selected_id == 0, "expiry kept selection");
    executor.publish(frame(4)); executor.snapshot();
    check(!executor.snapshot().moving, "expired AUTO restarted");
    check(executor.select(7) == ControlResult::Ok, "explicit reselection");
    until([&] { return executor.snapshot().moving; }, "reselection move");
    const auto generation = executor.snapshot().command_generation;
    executor.move(-1,1);
    auto state = executor.snapshot();
    check(!state.automatic && state.selected_id == 0 && state.command_generation > generation, "manual override ownership");
    auto events = log->read();
    check(events.size() >= 2 && events[events.size()-2].stop && events.back().x == -1, "STOP must precede manual MOVE");
    const auto count = events.size();
    executor.publish(frame(5)); std::this_thread::sleep_for(20ms);
    check(log->read().size() == count, "AUTO replay after manual override");
    executor.disconnected(); check(log->read().back().stop, "disconnect STOP");
    executor.connected(); check(!executor.snapshot().moving, "reconnect replay");
    executor.publish(frame(6)); check(executor.select(7) == ControlResult::Ok, "model test selection");
    until([&] { return executor.snapshot().moving; }, "model test move");
    runtime.generation.store(1);
    until([&] { return !executor.snapshot().automatic; }, "model generation did not invalidate selection");
    check(log->read().back().stop && executor.snapshot().selected_id == 0, "model switch STOP/clear");
    auto current = frame(7); current->model_generation = 1; executor.publish(current);
    check(executor.select(7) == ControlResult::Ok, "new generation selection");
    executor.shutdown(); executor.connected(); executor.move(1,0);
    check(!executor.snapshot().moving, "shutdown resurrected on connection");
    check(!log->multiple_owners && log->owner != std::this_thread::get_id(), "device has multiple I/O owners");
}
void ramp_reversal_and_bypass() {
    auto c = config(); c.automatic.period_ms = 20;
    c.automatic.input_timeout_ms = 1000; c.automatic.reacquire_timeout_ms = 1500;
    c.automatic.speed_min = 2; c.automatic.speed_rate = 100;
    c.automatic.kp = 60;
    RuntimeControl runtime; auto log = std::make_shared<Recording>();
    GimbalCommandExecutor e(c, std::make_unique<RecordingDevice>(log), runtime);
    e.connected(); e.publish(frame(1)); check(e.select(7) == ControlResult::Ok, "ramp selection");
    until([&] { return log->read().size() >= 5; }, "speed ramp did not progress");
    auto events = log->read();
    int previous = c.automatic.speed_min;
    for (const auto& event : events) {
        if (event.stop) continue;
        check(event.speed >= previous && event.speed - previous <= 3, "SPD changed faster than ramp plus quantization");
        check(event.speed <= c.automatic.speed_max, "SPD saturation exceeded");
        previous = event.speed;
    }
    auto reversed = frame(2); reversed->tracks[0] = track(160); e.publish(reversed);
    until([&] { return log->read().back().x == -1; }, "reversal did not resume");
    events = log->read();
    bool found = false;
    for (std::size_t i = 1; i < events.size(); ++i) {
        if (events[i].x == -1) {
            check(events[i-1].stop && events[i].speed <= 4, "reversal skipped STOP/minimum restart");
            found = true; break;
        }
    }
    check(found, "missing reversal command");
    auto bypass = frame(3); bypass->bypass = true; e.publish(bypass);
    until([&] { return e.snapshot().lost; }, "bypass drove AUTO");
    check(log->read().back().stop, "bypass did not STOP");
    runtime.state.store(RuntimeState::Switching);
    until([&] { return !e.snapshot().automatic; }, "switching did not clear selection");
    check(e.select(7) == ControlResult::NotReady, "switching accepted AUTO");
}
void invalid_inputs_and_error() {
    auto c = config(); c.heartbeat_timeout_ms = 150;
    c.automatic.input_timeout_ms = 500; c.automatic.reacquire_timeout_ms = 700;
    RuntimeControl runtime; auto log = std::make_shared<Recording>();
    GimbalCommandExecutor e(c, std::make_unique<RecordingDevice>(log), runtime);
    e.connected(); e.publish(frame(10)); check(e.select(7) == ControlResult::Ok,"selection");
    until([&] { return e.snapshot().moving; },"initial move");
    auto predicted = frame(11); predicted->tracks[0].predicted = true; e.publish(predicted);
    until([&] { return e.snapshot().lost; },"predicted output drove AUTO");
    e.publish(frame(9)); e.snapshot();
    check(e.snapshot().lost, "out-of-order frame replaced latest");
    auto stale = frame(12); auto stale_frame = std::make_shared<FrameContext>(*stale->frame);
    stale_frame->metadata.captured_at -= 1s; stale->frame = stale_frame; e.publish(stale); e.snapshot();
    check(e.select(7) == ControlResult::InvalidTarget,"stale target selected");
    e.publish(frame(13));
    until([&] { return e.snapshot().moving; },"fresh recovery");
    until([&] { return !e.snapshot().automatic; },"heartbeat independent STOP");
    check(log->read().back().stop, "heartbeat did not STOP");
    e.move(1,0); check(!e.snapshot().moving,"expired heartbeat MOVE");
    e.ping(); e.move(1,0); check(e.snapshot().moving,"PING did not renew liveness");
    e.move(0,0); check(log->read().back().stop && !e.snapshot().moving,"key release did not use STOP");
    e.move(1,0); e.halt(); check(log->read().back().stop && !e.snapshot().moving,"HOME halt did not STOP");
    e.move(1,0);
    log->fail = true; e.move(-1,0);
    check(e.snapshot().error && log->read().back().stop,"device error not latched/stopped");
    check(e.manual() == ControlResult::NotReady, "device error accepted MANUAL");
}
}
int main() try {
    calculations(); yaml_contract(); startup_failure_and_independent_poll(); state_and_watchdog(); ramp_reversal_and_bypass(); invalid_inputs_and_error();
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
