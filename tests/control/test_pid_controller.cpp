#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

#include "control/auto_tracking_controller.hpp"

using namespace std::chrono_literals;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double actual, double expected, const char* message) {
    check(std::abs(actual - expected) < 1e-6, message);
}
void modes() {
    PidAxisConfig c; c.kp = 2; c.ki = 3; c.kd = 4;
    c.integral_limit = 50; c.derivative_filter_ms = 100;
    for (auto type : {AutoControllerType::P, AutoControllerType::PI,
                      AutoControllerType::PD, AutoControllerType::PID}) {
        PidController pid(c, type, 100);
        const auto first = pid.calculate(1,0);
        near(first.value,2,"first sample must have no I/D kick");
        const auto second = pid.calculate(.5,.1);
        near(second.p,1,"P contribution");
        const bool use_i = type == AutoControllerType::PI || type == AutoControllerType::PID;
        const bool use_d = type == AutoControllerType::PD || type == AutoControllerType::PID;
        near(second.i,use_i ? .15 : 0,"selector must gate I independently of configured Ki");
        near(second.d,use_d ? -10 : 0,"filtered derivative/selector");
        near(second.value,1 + (use_i ? .15 : 0) - (use_d ? 10 : 0),"PID sum");
    }
}
void anti_windup() {
    PidAxisConfig c; c.kp = 1; c.ki = 10; c.integral_limit = 2;
    PidController bounded(c,AutoControllerType::PI,100);
    bounded.calculate(1,0);
    for (int i = 0; i < 100; ++i) bounded.calculate(1,.1);
    near(bounded.calculate(1,.1).i,2,"I contribution must remain bounded");
    PidController saturated(c,AutoControllerType::PI,5);
    saturated.calculate(10,0);
    for (int i = 0; i < 100; ++i) saturated.calculate(10,.1);
    near(saturated.calculate(10,.1).i,0,"output saturation accumulated integral");
    PidController rate_limited(c,AutoControllerType::PI,100);
    rate_limited.calculate(1,0); rate_limited.set_applied_output(0);
    near(rate_limited.calculate(1,.1).i,0,"rate-limited command accumulated integral");
    rate_limited.set_applied_output(1);
    near(rate_limited.calculate(1,.1).i,1,"integration did not resume after ramp caught up");
    rate_limited.set_applied_output(2);
    near(rate_limited.calculate(-.5,.1).i,.5,"opposite error must unwind integral");
    PidAxisConfig fine = c; fine.ki = 1;
    PidController quantized(fine,AutoControllerType::PI,100);
    quantized.calculate(1,0); quantized.set_applied_output(1);
    near(quantized.calculate(1,.1).i,.1,"initial fine integration");
    quantized.set_applied_output(1);
    near(quantized.calculate(1,.1).i,.2,"rounding residual permanently froze I");
    rate_limited.reset();
    near(rate_limited.calculate(1,0).i,0,"reset retained integral");
    const auto invalid = rate_limited.calculate(std::numeric_limits<double>::quiet_NaN(),.1);
    near(invalid.value,0,"invalid error did not clear controller");
}
Track target(double ex, double ey = 0) {
    Track t; t.track_id = 7; t.width = t.height = 20; t.confidence = .9;
    t.x = float(320 + ex * 320 - 10); t.y = float(240 + ey * 240 - 10);
    return t;
}
FrameMetadata metadata(std::uint64_t id, int ms) {
    return {id,std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(ms)};
}
AutoControlConfig pid_config() {
    AutoControlConfig c; c.controller = AutoControllerType::PID;
    c.pid_x.kp = c.pid_y.kp = 2;
    c.pid_x.ki = c.pid_y.ki = 1;
    c.pid_x.kd = c.pid_y.kd = .1;
    c.input_timeout_ms = 1000;
    return c;
}
void frame_time_and_reset() {
    auto c = pid_config(); AutoTrackingController controller(c);
    const auto first = controller.calculate(target(.5),640,480,metadata(1,1000));
    near(first.pid_x.d,0,"first derivative");
    const auto second = controller.calculate(target(.4),640,480,metadata(2,1100));
    near(second.pid_x.i,.04,"I must use capture dt");
    near(second.pid_x.d,-.05,"D must use capture dt and low-pass filter");
    for (int i = 0; i < 100; ++i) {
        const auto duplicate = controller.calculate(target(.1),640,480,metadata(2,1100));
        near(duplicate.error_x,second.error_x,"duplicate frame changed cached measurement");
        near(duplicate.pid_x.i,second.pid_x.i,"duplicate frame integrated");
        near(duplicate.pid_x.d,second.pid_x.d,"duplicate frame differentiated");
    }
    const auto third = controller.calculate(target(.3),640,480,metadata(3,1300));
    near(third.pid_x.i,.1,"variable capture dt integration");
    near(third.pid_x.d,-.05,"variable capture dt derivative");
    auto changed = target(.5); changed.track_id = 8;
    auto d = controller.calculate(changed,640,480,metadata(4,1400));
    near(d.pid_x.i,0,"target switch retained I"); near(d.pid_x.d,0,"target switch retained D");
    d = controller.calculate(changed,640,480,metadata(5,1500));
    check(d.pid_x.i > 0,"new target did not integrate");
    d = controller.calculate(changed,640,480,metadata(1,1600),1);
    near(d.pid_x.i,0,"model generation retained integral");
    d = controller.calculate(changed,640,480,metadata(2,1599),1);
    check(d.stop(),"backward capture time was used");
    check(controller.calculate(changed,640,480,metadata(2,1599),1).stop(),
          "repeated bad timestamp became a valid first measurement");
    d = controller.calculate(changed,640,480,metadata(3,1700),1);
    near(d.pid_x.d,0,"bad timestamp did not reset derivative");
    controller.calculate(changed,640,480,metadata(4,1800),1);
    d = controller.calculate(changed,640,480,metadata(5,3000),1);
    near(d.pid_x.i,0,"long frame gap retained integral");
    near(d.pid_x.d,0,"long frame gap retained derivative");
    controller.calculate(changed,640,480,metadata(6,3100),1);
    controller.reset();
    d = controller.calculate(changed,640,480,metadata(7,3200),1);
    near(d.pid_x.i,0,"explicit reset retained integral");
}
void deadband_and_signed_output() {
    auto c = pid_config(); AutoTrackingController controller(c);
    controller.calculate(target(.5,.5),640,480,metadata(1,1000));
    controller.calculate(target(.5,.5),640,480,metadata(2,1100));
    const auto x_idle = controller.calculate(target(0,.5),640,480,metadata(3,1200));
    check(x_idle.x == 0 && x_idle.y == -1,"axis Deadband changed active axis");
    near(x_idle.pid_x.i,0,"inactive axis retained I");
    check(x_idle.pid_y.i > 0,"active axis I was reset by other axis Deadband");
    const auto centered = controller.calculate(target(0,0),640,480,metadata(4,1300));
    check(centered.stop() && centered.speed == 0,"centered PID must STOP despite integral");
    const auto restart = controller.calculate(target(.5,.5),640,480,metadata(5,1400));
    near(restart.pid_x.i,0,"Deadband exit inherited I");
    near(restart.pid_x.d,0,"Deadband exit derivative kick");
    c.controller = AutoControllerType::PD;
    c.pid_x.kd = c.pid_y.kd = 1;
    AutoTrackingController braking(c);
    braking.calculate(target(.8,.8),640,480,metadata(1,1000));
    const auto d = braking.calculate(target(.1,.1),640,480,metadata(2,1100));
    check(d.error_x > 0 && d.pid_x.value < 0 && d.x == -1,"X direction must follow signed PD output");
    check(d.error_y > 0 && d.pid_y.value < 0 && d.y == 1,"Y conversion must follow signed PD output");
}
void yaml_selection() {
    auto yaml = YAML::Load("enabled: true\nauto: {controller: pid, kp: 12, pid_x: {ki: 2, kd: 3}, pid_y: {kp: 8}}\n");
    const auto parsed = config_parser::load_control_config(yaml).automatic;
    check(parsed.controller == AutoControllerType::PID,"PID selection");
    near(parsed.pid_x.kp,12,"legacy common kp fallback");
    near(parsed.pid_y.kp,8,"axis kp override");
    near(parsed.pid_x.ki,2,"Ki parsing"); near(parsed.pid_x.kd,3,"Kd parsing");
    for (const auto* name : {"p","pi","pd","pid"}) {
        yaml["auto"]["controller"] = name;
        config_parser::load_control_config(yaml);
    }
    const auto rejects = [&] {
        try { config_parser::load_control_config(yaml); return false; }
        catch (const std::runtime_error&) { return true; }
    };
    yaml["auto"]["controller"] = "unknown"; check(rejects(),"unknown controller accepted");
    yaml["auto"]["controller"] = "p";
    yaml["auto"]["pid_x"]["ki"] = -1; check(rejects(),"negative Ki accepted");
    yaml["auto"]["pid_x"]["ki"] = 0;
    yaml["auto"]["pid_x"]["kd"] = std::numeric_limits<double>::infinity(); check(rejects(),"nonfinite Kd accepted");
    yaml["auto"]["pid_x"]["kd"] = 0;
    yaml["auto"]["pid_y"]["derivative_filter_ms"] = 0; check(rejects(),"zero derivative filter accepted");
    yaml["auto"]["pid_y"]["derivative_filter_ms"] = 100;
    yaml["auto"]["pid_x"]["integral_limit"] = -1; check(rejects(),"negative I limit accepted");
    yaml["auto"].remove("pid_x"); yaml["auto"].remove("pid_y"); yaml["auto"].remove("controller");
    check(config_parser::load_control_config(yaml).automatic.controller == AutoControllerType::P,"omitted selector must preserve P");
}
}
int main() try {
    modes(); anti_windup(); frame_time_and_reset(); deadband_and_signed_output(); yaml_selection();
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
