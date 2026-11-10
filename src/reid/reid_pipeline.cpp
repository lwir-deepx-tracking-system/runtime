#include "reid/reid_pipeline.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>
#include <opencv2/imgproc.hpp>

LwirReidPreprocessor::LwirReidPreprocessor(
    const ReidModelConfig& model, const CameraInputConfig& camera)
    : model_(model), camera_(camera)
{
    if (model_.input_width <= 0 || model_.input_height <= 0 ||
        model_.input_channels != 3 || model_.resize != "stretch" ||
        model_.channel_mode != "replicate_gray_to_rgb" ||
        camera_.opencv_type != "CV_16UC1" || camera_.clip_max <= camera_.clip_min)
        throw std::invalid_argument("Unsupported ReID preprocessing contract");
}

cv::Mat LwirReidPreprocessor::process(const cv::Mat& image, const cv::Rect& crop) const
{
    if (image.type() != CV_16UC1 || image.cols != camera_.width ||
        image.rows != camera_.height || crop.width <= 0 || crop.height <= 0 ||
        (crop & cv::Rect(0, 0, image.cols, image.rows)) != crop)
        throw std::invalid_argument("ReID crop is outside the configured LWIR frame");

    cv::Mat gray8;
    const double range = static_cast<double>(camera_.clip_max - camera_.clip_min);
    image(crop).convertTo(gray8, CV_8UC1, 255.0 / range,
        -static_cast<double>(camera_.clip_min) * 255.0 / range);

    cv::Mat resized;
    if (gray8.cols == model_.input_width && gray8.rows == model_.input_height)
        resized = gray8;
    else
        cv::resize(gray8, resized, cv::Size(model_.input_width, model_.input_height),
            0.0, 0.0, cv::INTER_LINEAR);

    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_GRAY2RGB);
    return rgb;
}

L2ReidPostprocessor::L2ReidPostprocessor(int dimensions) : dimensions_(dimensions)
{
    if (dimensions_ <= 0)
        throw std::invalid_argument("ReID embedding dimensions must be positive");
}

std::vector<float> L2ReidPostprocessor::process(std::vector<float> embedding) const
{
    if (embedding.size() != static_cast<std::size_t>(dimensions_))
        throw std::runtime_error("ReID embedding dimensions do not match configuration");
    double norm_sq = 0.0;
    for (float value : embedding)
    {
        if (!std::isfinite(value))
            throw std::runtime_error("ReID embedding is not finite");
        norm_sq += static_cast<double>(value) * value;
    }
    if (!std::isfinite(norm_sq) || norm_sq <= 0.0)
        throw std::runtime_error("ReID embedding has invalid norm");
    const float inverse_norm = static_cast<float>(1.0 / std::sqrt(norm_sq));
    for (float& value : embedding) value *= inverse_norm;
    return embedding;
}

ReidPipeline::ReidPipeline(std::unique_ptr<IReidPreprocessor> preprocessor,
    std::unique_ptr<IReidModel> model,
    std::unique_ptr<IReidPostprocessor> postprocessor)
    : preprocessor_(std::move(preprocessor)), model_(std::move(model)),
      postprocessor_(std::move(postprocessor))
{
    if (!preprocessor_ || !model_ || !postprocessor_)
        throw std::invalid_argument("ReID pipeline stages must not be null");
}

std::vector<float> ReidPipeline::embed(const cv::Mat& image, const cv::Rect& crop)
{
    cv::Mat input = preprocessor_->process(image, crop);
    return postprocessor_->process(model_->infer(input));
}
