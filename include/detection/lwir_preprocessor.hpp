#pragma once

#include "app/app_config.hpp"

#include <opencv2/core.hpp>

struct LwirPreprocessContext
{
    int original_width = 0;
    int original_height = 0;
    int input_width = 0;
    int input_height = 0;
    int pad_x = 0;
    int pad_y = 0;
    float scale = 1.0F;
};

class LwirPreprocessor
{
public:
    explicit LwirPreprocessor(const ModelConfig& config);
    void process(const cv::Mat& input, cv::Mat& output, LwirPreprocessContext& context) const;
    int input_width() const noexcept { return config_.input.width; }
    int input_height() const noexcept { return config_.input.height; }

private:
    ModelConfig config_;
};
