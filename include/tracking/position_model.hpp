#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Fixed 68-input model-bank keypoint arms; no Python or NumPy at runtime.
class PositionModel {
public:
    struct State {
        std::array<double,64> hidden{};
        std::array<double,96*3> conv{};
        std::array<double,96*8> ssm{};
    };
    explicit PositionModel(const std::string& path);
    std::array<double,4> step(const std::array<double,68>& input, State& state) const;
    bool no_edges() const { return flags_ & 1u; }
    bool warp_fix() const { return flags_ & 2u; }
    bool box_mode_targets() const { return flags_ & 4u; }
private:
    std::uint32_t kind_ = 0, flags_ = 0;
    std::vector<double> weights_;
};
