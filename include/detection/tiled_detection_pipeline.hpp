#pragma once

#include "detection/i_detection_pipeline.hpp"
#include "detection/i_preprocessor.hpp"

#include <array>
#include <mutex>

// Private tile adapter. The parent already mapped the entire raw frame to gray8.
class TilePreprocessor final : public IPreprocessor {
public:
    using IPreprocessor::IPreprocessor;
    int input_width() const noexcept override;
    int input_height() const noexcept override;

    void process(const cv::Mat& gray, cv::Mat& output, PreprocessContext& context) const override;
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
            std::exception_ptr failure, DetectionTiming timing);
    };

    static float iou(const Detection& a, const Detection& b);

    static std::vector<Detection> merge(std::array<std::vector<Detection>, 2>& tiles);

    std::array<std::unique_ptr<IDetectionPipeline>, 2> tiles_;
    std::mutex submit_mutex_;
    bool mapped_ = false;
    float low_ = 0, high_ = 0;

    // Model-bank uses a 4096-bin full-frame histogram and a temporal EMA.
    cv::Mat map_frame(const cv::Mat& raw);

public:
    TiledDetectionPipeline(std::unique_ptr<IDetectionPipeline> left,
        std::unique_ptr<IDetectionPipeline> right);
    ~TiledDetectionPipeline() override;

    void submit_async(const FrameContextPtr& frame, Completion completion) override;

    void wait_idle() noexcept override;
};
