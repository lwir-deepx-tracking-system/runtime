#pragma once

#include "web_gui/video.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <zlib.h>

namespace web_gui {
// Reads Board recordings and presents stored tracking output without using the live NPU.
class Replay {
public:
    explicit Replay(std::filesystem::path root);
    ~Replay();
    nlohmann::json listing(const std::vector<std::string>& names);
    nlohmann::json state();
    nlohmann::json evaluation(std::optional<unsigned> prefix = {}, bool final = false);
    bool active();
    void start(const std::string& name, unsigned fps, bool loop);
    void stop();
    void seek(unsigned frame);
    void speed(unsigned fps);
    void play(bool on);
    void step();
    void restart();
    void draw_truth(bool on);
    void select(double x, double y);
    void zoom(double value);
    std::shared_ptr<const Frame> picture();

private:
    struct Box {
        std::uint32_t id;
        double x, y, w, h;
    };
    using Boxes = std::vector<Box>;
    void advance();
    void position(std::size_t index);
    void require_active() const;
    nlohmann::json evaluate(std::size_t end, bool complete);
    struct Evaluation {
        std::size_t next = 0, gt = 0, found = 0, false_count = 0, missed_small = 0;
        std::map<std::uint32_t, std::uint32_t> identities;
        nlohmann::json switches = nlohmann::json::array();
    } score_;

    std::filesystem::path root_;
    std::mutex mutex_;
    gzFile input_ = nullptr;
    std::string name_;
    std::vector<std::uint64_t> frames_;
    std::map<std::uint64_t, Boxes> tracks_;
    std::map<std::uint64_t, Boxes> truth_;
    std::size_t index_ = 0;
    std::size_t gzip_index_ = 0;
    unsigned fps_ = 25;
    bool loop_ = false, paused_ = false, draw_ = false;
    Clock::time_point due_;
    std::uint32_t target_ = 0;
    double zoom_ = 1.0;
    bool complete_ = false;
    unsigned crop_x_ = 0, crop_y_ = 0, crop_w_ = kGuiFrameWidth, crop_h_ = kGuiFrameHeight;
    std::shared_ptr<const Frame> picture_;
    std::uint64_t sequence_ = 0;
};
}
