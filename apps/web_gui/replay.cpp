#include "replay.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace web_gui {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kFrameBytes = kGuiFrameWidth * kGuiFrameHeight * 2;
constexpr std::size_t kMaxFrames = 250000;

// Only folders produced by the Runtime recorder are accepted.
bool recording_name(const std::string& name)
{
    return name.size() >= 10 && name.size() <= 64 && name.rfind("MEASURE_", 0) == 0 &&
           std::all_of(name.begin() + 8, name.end(), [](char c) {
               return (c >= '0' && c <= '9') || c == '_';
           });
}

std::vector<std::uint64_t> read_frames(const std::filesystem::path& file)
{
    std::ifstream input(file);
    std::string line;
    std::getline(input, line);
    if (line != "raw_frame,frame_id,capture_steady_ns") return {};
    std::vector<std::uint64_t> frames;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::uint64_t seq, frame, timestamp;
        char a, b;
        if (!(row >> seq >> a >> frame >> b >> timestamp) || a != ',' || b != ',' ||
            seq != frames.size() || !frame || (!frames.empty() && frame <= frames.back()))
            throw std::runtime_error("Invalid recording frame index");
        frames.push_back(frame);
        if (frames.size() > kMaxFrames) throw std::runtime_error("Recording exceeds replay frame limit");
    }
    return frames;
}
}

Replay::Replay(std::filesystem::path root) : root_(std::move(root)) {}
Replay::~Replay() { if (input_) gzclose(input_); }

Json Replay::listing(const std::vector<std::string>& names)
{
    auto result = Json::array();
    for (const auto& name : names) {
        if (!recording_name(name)) continue;
        const auto path = root_ / name;
        if (std::filesystem::is_symlink(path) ||
            !std::filesystem::is_regular_file(path / "frames.raw.gz")) continue;
        try {
            const auto size = read_frames(path / "frames.csv").size();
            if (size) result.push_back({{"name", name}, {"frames", size}, {"bits", 16}, {"usable", true}});
        } catch (const std::runtime_error&) {}
    }
    return result;
}

void Replay::start(const std::string& name, unsigned fps, bool loop)
{
    if (!recording_name(name)) throw std::invalid_argument("잘못된 녹화 이름");
    const auto path = root_ / name;
    if (std::filesystem::is_symlink(path)) throw std::invalid_argument("잘못된 녹화 경로");
    auto frames = read_frames(path / "frames.csv");
    if (frames.empty()) throw std::invalid_argument("재생 가능한 영상이 없습니다");
    std::map<std::uint64_t, Boxes> tracks, truth;
    const auto read_boxes = [&](const std::filesystem::path& file, bool labels, auto& output) {
        std::ifstream input(file);
        std::string line;
        if (!labels) std::getline(input, line);
        while (std::getline(input, line)) {
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream row(line);
            std::uint64_t frame;
            Box box{};
            if (!(row >> frame >> box.id)) continue;
            int label, predicted = 0;
            double confidence;
            if (!labels && !(row >> label >> confidence)) continue;
            if (!(row >> box.x >> box.y >> box.w >> box.h) ||
                !std::isfinite(box.x) || !std::isfinite(box.y) ||
                !std::isfinite(box.w) || !std::isfinite(box.h) ||
                box.w <= 0 || box.h <= 0) continue;
            if (!labels && (!(row >> predicted) || predicted || label != 0)) continue;
            if (labels) {
                if (!frame || frame > frames.size()) continue;
                if (row >> confidence && confidence <= 0) continue;
                if (row >> label && label != 1) continue;
                frame = frames[frame - 1];
            } else if (!std::binary_search(frames.begin(), frames.end(), frame)) continue;
            const auto right = std::clamp(box.x + box.w, 0.0, double(kGuiFrameWidth));
            const auto bottom = std::clamp(box.y + box.h, 0.0, double(kGuiFrameHeight));
            box.x = std::clamp(box.x, 0.0, double(kGuiFrameWidth));
            box.y = std::clamp(box.y, 0.0, double(kGuiFrameHeight));
            box.w = right - box.x;
            box.h = bottom - box.y;
            if (box.w <= 0 || box.h <= 0) continue;
            auto& boxes = output[frame];
            if (boxes.size() < 256) boxes.push_back(box);
        }
    };
    read_boxes(path / "tracks.csv", false, tracks);
    read_boxes(path / "gt" / "gt.txt", true, truth);
    auto* input = gzopen((path / "frames.raw.gz").c_str(), "rb");
    if (!input) throw std::runtime_error("녹화 영상을 열 수 없습니다");

    std::lock_guard lock(mutex_);
    if (input_) gzclose(input_);
    input_ = input;
    name_ = name;
    frames_ = std::move(frames);
    tracks_ = std::move(tracks);
    truth_ = std::move(truth);
    fps_ = fps;
    paused_ = false;
    loop_ = loop;
    target_ = 0;
    zoom_ = 1.0;
    score_ = {};
    complete_ = false;
    gzip_index_ = 0;
    position(0);
}

void Replay::stop()
{
    std::lock_guard lock(mutex_);
    if (input_) gzclose(input_);
    input_ = nullptr;
    picture_.reset();
}

void Replay::require_active() const
{
    if (!input_) throw std::invalid_argument("재생 중인 녹화가 없습니다");
}

void Replay::position(std::size_t index)
{
    index_ = std::min(index, frames_.size() - 1);
    due_ = Clock::now() + std::chrono::milliseconds(1000 / fps_);
    picture_.reset();
}

void Replay::advance()
{
    if (!input_ || paused_ || !picture_ || Clock::now() < due_) return;
    if (index_ + 1 < frames_.size()) position(index_ + 1);
    else {
        complete_ = true;
        if (loop_) position(0);
        else paused_ = true;
    }
}

bool Replay::active()
{
    std::lock_guard lock(mutex_);
    return input_;
}

void Replay::seek(unsigned frame)
{
    std::lock_guard lock(mutex_);
    require_active();
    position(frame);
}

void Replay::speed(unsigned fps)
{
    std::lock_guard lock(mutex_);
    require_active();
    fps_ = fps;
    due_ = Clock::now() + std::chrono::milliseconds(1000 / fps_);
}

void Replay::play(bool on)
{
    std::lock_guard lock(mutex_);
    require_active();
    if (on && index_ + 1 == frames_.size()) position(0);
    paused_ = !on;
    due_ = Clock::now() + std::chrono::milliseconds(1000 / fps_);
}

void Replay::step()
{
    std::lock_guard lock(mutex_);
    require_active();
    paused_ = true;
    position(index_ + 1);
}

void Replay::restart()
{
    std::lock_guard lock(mutex_);
    require_active();
    paused_ = loop_ = complete_ = false;
    score_ = {};
    position(0);
}

void Replay::draw_truth(bool on)
{
    std::lock_guard lock(mutex_);
    draw_ = on;
    picture_.reset();
}

void Replay::select(double x, double y)
{
    std::lock_guard lock(mutex_);
    require_active();
    target_ = 0;
    x = crop_x_ + x * crop_w_ / kGuiFrameWidth;
    y = crop_y_ + y * crop_h_ / kGuiFrameHeight;
    if (const auto boxes = tracks_.find(frames_[index_]); boxes != tracks_.end())
        for (const auto& box : boxes->second)
            if (x >= box.x && x <= box.x + box.w && y >= box.y && y <= box.y + box.h) {
                target_ = box.id;
                break;
            }
    picture_.reset();
}

void Replay::zoom(double value)
{
    std::lock_guard lock(mutex_);
    require_active();
    zoom_ = value;
    picture_.reset();
}

Json Replay::state()
{
    std::lock_guard lock(mutex_);
    return {{"on", input_ != nullptr}, {"name", name_}, {"backend", "stored"},
            {"pos", index_}, {"total", frames_.size()}, {"paused", paused_},
            {"loop", loop_}, {"fps", fps_}};
}

std::shared_ptr<const Frame> Replay::picture()
{
    std::lock_guard lock(mutex_);
    if (!input_) return nullptr;
    advance();
    if (picture_) return picture_;
    if (index_ != gzip_index_ && gzseek(input_, static_cast<z_off_t>(index_) * kFrameBytes, SEEK_SET) < 0)
        throw std::runtime_error("녹화 위치를 읽을 수 없습니다");
    std::vector<unsigned char> raw(kFrameBytes);
    if (gzread(input_, raw.data(), raw.size()) != static_cast<int>(raw.size()))
        throw std::runtime_error("녹화 프레임이 손상됐습니다");
    gzip_index_ = index_ + 1;
    cv::Mat gray(kGuiFrameHeight, kGuiFrameWidth, CV_8UC1);
    int low = 65535, high = 0;
    const auto value = [&](std::size_t i) { return int(raw[2*i]) | (int(raw[2*i+1]) << 8); };
    for (std::size_t i = 0; i < gray.total(); ++i) {
        low = std::min(low, value(i));
        high = std::max(high, value(i));
    }
    for (std::size_t i = 0; i < gray.total(); ++i)
        gray.data[i] = (value(i) - low) * 255 / std::max(1, high - low);
    cv::Mat color;
    cv::cvtColor(gray, color, cv::COLOR_GRAY2BGR);
    cv::Point2d center(color.cols / 2.0, color.rows / 2.0);
    const auto paint = [&](const Boxes& boxes, bool truth) {
        for (const auto& box : boxes) {
            const auto selected = !truth && target_ && box.id == target_;
            if (selected) center = {box.x + box.w / 2, box.y + box.h / 2};
            const cv::Scalar pen = truth ? cv::Scalar(80, 255, 80) :
                selected ? cv::Scalar(0, 220, 255) : cv::Scalar(60, 170, 255);
            cv::rectangle(color, cv::Rect2d(box.x, box.y, box.w, box.h), pen, 2);
            cv::putText(color, std::to_string(box.id), {int(box.x), std::max(14, int(box.y) - 3)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.45, pen);
        }
    };
    const auto frame = frames_[index_];
    if (auto it = tracks_.find(frame); it != tracks_.end()) paint(it->second, false);
    if (auto it = truth_.find(frame); draw_ && it != truth_.end()) paint(it->second, true);
    const int w = color.cols / zoom_, h = color.rows / zoom_;
    const int x = std::clamp(int(center.x) - w / 2, 0, color.cols - w);
    const int y = std::clamp(int(center.y) - h / 2, 0, color.rows - h);
    crop_x_ = x; crop_y_ = y; crop_w_ = w; crop_h_ = h;
    cv::Mat view;
    cv::resize(color(cv::Rect(x, y, w, h)), view, color.size());
    std::vector<unsigned char> jpeg;
    if (!cv::imencode(".jpg", view, jpeg)) throw std::runtime_error("JPEG encoding failed");
    picture_ = std::make_shared<Frame>(Frame{++sequence_, {jpeg.begin(), jpeg.end()}, {}, Clock::now()});
    return picture_;
}

Json Replay::evaluation(std::optional<unsigned> prefix, bool final)
{
    std::lock_guard lock(mutex_);
    if (frames_.empty()) return Json::object();
    if (prefix && *prefix >= frames_.size())
        return {{"prefix_unavailable", true}, {"why", "녹화 범위를 벗어났습니다"}};
    const auto end = final ? frames_.size() - 1 : prefix ? *prefix : index_;
    auto out = evaluate(end, final || (!prefix && complete_));
    if (prefix) out["prefix_pos"] = *prefix;
    return out;
}

Json Replay::evaluate(std::size_t end, bool complete)
{
    Json out{{"name", name_}, {"on", input_ != nullptr}, {"draw", draw_}, {"backend", "stored"},
             {"phase", complete ? "complete" : input_ ? "scoring" : "interrupted"},
             {"scoring", !truth_.empty()}, {"why", "MOT 정답 파일이 없습니다"},
             {"label_file", "gt/gt.txt"}, {"review", "저장 추적 결과 · 사람 IoU ≥ 0.5"},
             {"metric_status", "unavailable"}, {"detector_metrics", false}, {"since", "첫 프레임"},
             {"sequence_frames", frames_.size()}, {"pass_frames", end + 1}};
    if (truth_.empty()) return out;
    if (score_.next > end + 1) score_ = {};
    auto& [next, gt_count, found, false_count, missed_small, identities, switches] = score_;
    const Boxes empty;
    for (; next <= end; ++next) {
        const auto i = next;
        const auto gt = truth_.find(frames_[i]);
        const auto track = tracks_.find(frames_[i]);
        const auto& actual = gt == truth_.end() ? empty : gt->second;
        const auto& predicted = track == tracks_.end() ? empty : track->second;
        std::set<std::size_t> used;
        gt_count += actual.size();
        for (const auto& a : actual) {
            std::size_t best = predicted.size();
            double score = 0.5;
            for (std::size_t j = 0; j < predicted.size(); ++j) {
                if (used.count(j)) continue;
                const auto& b = predicted[j];
                const auto w = std::max(0.0, std::min(a.x+a.w,b.x+b.w) - std::max(a.x,b.x));
                const auto h = std::max(0.0, std::min(a.y+a.h,b.y+b.h) - std::max(a.y,b.y));
                const auto iou = w*h / (a.w*a.h + b.w*b.h - w*h);
                if (iou >= score) { score = iou; best = j; }
            }
            if (best == predicted.size()) { if (a.h < 32) ++missed_small; continue; }
            used.insert(best);
            ++found;
            const auto id = predicted[best].id;
            if (identities.count(a.id) && identities[a.id] != id)
                switches.push_back({{"frame", i+1}, {"gt_id", a.id},
                                    {"from_id", identities[a.id]}, {"to_id", id}});
            identities[a.id] = id;
        }
        false_count += predicted.size() - used.size();
    }
    out["counts"] = {{"scored", end + 1}};
    const auto ratio = [](double n, std::size_t d) { return d ? Json(100.0*n/d) : Json(nullptr); };
    out["new"] = {{"gt", gt_count}, {"found", found}, {"false", false_count},
                  {"missed", gt_count-found}, {"missed_small", missed_small},
                  {"missed_other", gt_count-found-missed_small}, {"idsw", switches.size()},
                  {"recall", ratio(found, gt_count)}, {"precision", ratio(found, found+false_count)},
                  {"mota", ratio(double(found)-false_count-switches.size(), gt_count)}};
    out["old"] = out["new"];
    out["id_switches"] = switches;
    return out;
}
}
