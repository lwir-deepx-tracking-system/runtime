#include "detection/lwir_preprocessor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <opencv2/imgproc.hpp>

LwirPreprocessor::LwirPreprocessor(const ModelConfig& config) : config_(config) {}

void LwirPreprocessor::process(
    const cv::Mat& input, cv::Mat& output, LwirPreprocessContext& context) const
{
    if (input.empty()) throw std::invalid_argument("LWIR input is empty");
    if (input.type() != CV_16UC1) throw std::invalid_argument("LWIR input must be CV_16UC1");
    if (input.cols != config_.camera_input.width || input.rows != config_.camera_input.height)
        throw std::invalid_argument("LWIR input dimensions do not match camera_input configuration");
    if (input.step < static_cast<std::size_t>(input.cols) * sizeof(std::uint16_t))
        throw std::invalid_argument("LWIR input stride is smaller than one image row");

    const double range = static_cast<double>(config_.camera_input.clip_max - config_.camera_input.clip_min);
    cv::Mat gray8;
    input.convertTo(gray8, CV_8UC1, 255.0 / range,
                    -static_cast<double>(config_.camera_input.clip_min) * 255.0 / range);

    cv::Mat rgb;
    cv::cvtColor(gray8, rgb, cv::COLOR_GRAY2RGB);
    const float scale = std::min(
        static_cast<float>(config_.input.width) / static_cast<float>(input.cols),
        static_cast<float>(config_.input.height) / static_cast<float>(input.rows));
    const int resized_width = static_cast<int>(std::round(input.cols * scale));
    const int resized_height = static_cast<int>(std::round(input.rows * scale));
    const int total_x = config_.input.width - resized_width;
    const int total_y = config_.input.height - resized_height;
    const int left = total_x / 2;
    const int top = total_y / 2;

    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(resized_width, resized_height), 0.0, 0.0, cv::INTER_LINEAR);
    cv::copyMakeBorder(resized, output, top, total_y - top, left, total_x - left,
                       cv::BORDER_CONSTANT, cv::Scalar::all(config_.preprocess.pad_value));

    context.original_width = input.cols;
    context.original_height = input.rows;
    context.input_width = output.cols;
    context.input_height = output.rows;
    context.pad_x = left;
    context.pad_y = top;
    context.scale = scale;
}
