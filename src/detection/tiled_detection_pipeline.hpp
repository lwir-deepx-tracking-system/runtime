#pragma once

#include "detection/i_detection_pipeline.hpp"
#include "detection/i_preprocessor.hpp"
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>
#include <utility>

// Private tile adapter. The parent already mapped the entire raw frame to gray8.
class TilePreprocessor final : public IPreprocessor {
public:
    using IPreprocessor::IPreprocessor;
    int input_width() const noexcept override { return 384; }
    int input_height() const noexcept override { return 480; }

    void process(const cv::Mat& gray, cv::Mat& output, PreprocessContext& context) const override
    {
        if (gray.type() != CV_8UC1 || gray.size() != cv::Size(input_width(), input_height()))
            throw std::invalid_argument("Invalid tile input");
        cv::cvtColor(gray, output, cv::COLOR_GRAY2RGB);
        context = {gray.cols, gray.rows, gray.cols, gray.rows, 0, 0, 1.0f, false};
    }
};

// Owns the two child sessions and completes one request after both tiles finish.
class TiledDetectionPipeline final : public IDetectionPipeline {
    static constexpr int width = 640, height = 480, tile_width = 384;
    static constexpr int offset = width - tile_width, seam = width / 2;

    struct Request {
        std::mutex mutex;
        unsigned pending = 2;
        std::array<std::vector<Detection>, 2> detections;
        std::array<DetectionTiming, 2> timings;
        std::exception_ptr error;
        Completion completion;

        // Join by tile index so equal-confidence results have deterministic ordering.
        void finish(unsigned side, std::vector<Detection> boxes,
            std::exception_ptr failure, DetectionTiming timing)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                detections[side] = std::move(boxes);
                timings[side] = std::move(timing);
                if (failure && !error) error = failure;
                if (--pending != 0) return;
            }
            auto total = std::move(timings[0]);
            std::vector<Detection> merged;
            try {
                if (!error) {
                    merged = merge(detections);
                    const auto& right = timings[1];
                    total.preprocess_finished_at = std::max(total.preprocess_finished_at, right.preprocess_finished_at);
                    total.inference_finished_at = std::max(total.inference_finished_at, right.inference_finished_at);
                    total.postprocess_started_at = std::min(total.postprocess_started_at, right.postprocess_started_at);
                    total.postprocess_finished_at = DetectionTiming::Clock::now();
                    total.npu_processing_duration_ms = std::max(total.npu_processing_duration_ms, right.npu_processing_duration_ms);
                    total.npu_profiler_valid = total.npu_profiler_valid && right.npu_profiler_valid;
                    total.npu_devices.insert(total.npu_devices.end(), right.npu_devices.begin(), right.npu_devices.end());
                }
            } catch (...) { error = std::current_exception(); }
            try { completion(std::move(merged), error, std::move(total)); } catch (...) {}
        }
    };

    static float iou(const Detection& a, const Detection& b)
    {
        const float w = std::max(0.f, std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x));
        const float h = std::max(0.f, std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y));
        const float overlap = w * h;
        const float area = a.width * a.height + b.width * b.height - overlap;
        return area > 0.f ? overlap / area : 0.f;
    }

    static std::vector<Detection> merge(std::array<std::vector<Detection>, 2>& tiles)
    {
        std::vector<Detection> candidates;
        candidates.reserve(tiles[0].size() + tiles[1].size());
        for (unsigned side = 0; side < 2; ++side) {
            for (auto& box : tiles[side]) {
                box.x += side * offset;
                const float center = box.x + box.width * .5f;
                if (side == 0 ? center >= seam : center < seam) continue;
                for (auto& point : box.keypoints) point.x += side * offset;
                candidates.push_back(std::move(box));
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(),
            [](const Detection& a, const Detection& b) { return a.confidence > b.confidence; });
        std::vector<Detection> selected;
        for (auto& candidate : candidates) {
            if (std::none_of(selected.begin(), selected.end(), [&](const Detection& box) {
                return box.class_id == candidate.class_id && iou(box, candidate) > .7f;
            })) selected.push_back(std::move(candidate));
            if (selected.size() == 300) break;
        }
        for (auto& box : selected) {
            const auto x2 = std::clamp(box.x + box.width, 0.f, static_cast<float>(width));
            const auto y2 = std::clamp(box.y + box.height, 0.f, static_cast<float>(height));
            box.x = std::clamp(box.x, 0.f, static_cast<float>(width));
            box.y = std::clamp(box.y, 0.f, static_cast<float>(height));
            box.width = x2 - box.x;
            box.height = y2 - box.y;
        }
        selected.erase(std::remove_if(selected.begin(), selected.end(), [](const Detection& box) {
            return box.width <= 0 || box.height <= 0;
        }), selected.end());
        return selected;
    }

    std::array<std::unique_ptr<IDetectionPipeline>, 2> tiles_;
    std::mutex submit_mutex_;
    bool mapped_ = false;
    float low_ = 0, high_ = 0;

    // Model-bank uses a 4096-bin full-frame histogram and a temporal EMA.
    cv::Mat map_frame(const cv::Mat& raw)
    {
        std::array<unsigned, 4096> histogram{};
        for (int y = 0; y < height; ++y) {
            const auto* row = raw.ptr<std::uint16_t>(y);
            for (int x = 0; x < width; ++x) ++histogram[row[x] >> 4];
        }
        unsigned count = 0;
        float low = -1, high = 0;
        for (unsigned bin = 0; bin < histogram.size(); ++bin) {
            count += histogram[bin];
            if (low < 0 && count >= width * height * .01) low = bin * 16.f;
            if (count >= width * height * .99) { high = bin * 16.f + 16.f; break; }
        }
        high = std::max(high, low + 200.f);
        if (mapped_) {
            low_ += .1f * (low - low_);
            high_ += .1f * (high - high_);
        } else {
            low_ = low;
            high_ = high;
            mapped_ = true;
        }
        const auto scale = 255.f / (high_ - low_);
        cv::Mat gray(height, width, CV_8UC1);
        for (int y = 0; y < height; ++y) {
            const auto* in = raw.ptr<std::uint16_t>(y);
            auto* out = gray.ptr<std::uint8_t>(y);
            for (int x = 0; x < width; ++x)
                out[x] = static_cast<std::uint8_t>(std::clamp((in[x] - low_) * scale, 0.f, 255.f));
        }
        return gray;
    }

public:
    TiledDetectionPipeline(std::unique_ptr<IDetectionPipeline> left,
        std::unique_ptr<IDetectionPipeline> right) : tiles_{std::move(left), std::move(right)} {}
    ~TiledDetectionPipeline() override { wait_idle(); }

    void submit_async(const FrameContextPtr& frame, Completion completion) override
    {
        if (!frame || frame->image.type() != CV_16UC1 || frame->image.size() != cv::Size(width, height))
            throw std::invalid_argument("Tiled detection requires a 640x480 CV_16UC1 frame");
        std::lock_guard<std::mutex> lock(submit_mutex_);
        const auto started = DetectionTiming::Clock::now();
        const auto gray = map_frame(frame->image);
        auto request = std::make_shared<Request>();
        request->completion = std::move(completion);
        for (unsigned side = 0; side < 2; ++side) {
            try {
                auto tile = std::make_shared<FrameContext>();
                tile->metadata = frame->metadata;
                tile->image = gray(cv::Rect(side * offset, 0, tile_width, height));
                tiles_[side]->submit_async(tile, [request, side, started](auto boxes, auto error, auto timing) {
                    timing.preprocess_started_at = started;
                    request->finish(side, std::move(boxes), error, std::move(timing));
                });
            } catch (...) {
                DetectionTiming timing;
                timing.frame_id = frame->metadata.frame_id;
                request->finish(side, {}, std::current_exception(), std::move(timing));
            }
        }
    }

    void wait_idle() noexcept override
    {
        for (auto& tile : tiles_) tile->wait_idle();
    }
};
