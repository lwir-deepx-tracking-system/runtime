#pragma once

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <string>

#include "common/detection.hpp"
#include "tracking/camera_motion.hpp"
#include "tracking/position_model.hpp"

// Per-track keypoint history and recurrent prediction for lost tracks.
class PositionMemory {
public:
    explicit PositionMemory(const std::string& mode);
    bool enabled() const { return static_cast<bool>(model_); }
    void observe(int id, const Detection& detection, const KalmanState& state,
                 const KalmanState* prediction = nullptr);
    void lost(int id, KalmanState& state, const CameraMotion& first_warp,
              const CameraMotion& current_warp);
    void forget(int id) { tracks_.erase(id); }
    void reset() { tracks_.clear(); }
private:
    struct Entry {
        std::deque<std::array<double,68>> history;
        std::array<double,4> previous{}, base{}, plain{};
        std::array<double,2> velocity{}, size_velocity{};
        PositionModel::State recurrent{};
        int missed = 0;
        double initial_height = 1;
        bool last_has_keypoint = false;
    };
    std::unique_ptr<PositionModel> model_;
    std::map<int,Entry> tracks_;
};
