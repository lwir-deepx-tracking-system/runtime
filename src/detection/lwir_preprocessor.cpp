#include "detection/lwir_preprocessor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <opencv2/imgproc.hpp>

// 실행 중 YAML을 다시 읽지 않고 AppConfig가 검증한 값을 복사해 사용한다.
LwirPreprocessor::LwirPreprocessor(const ModelConfig& config) : IPreprocessor(config)
{
    const auto& camera = config_.camera_input;
    const auto& target = config_.input;
    if (camera.width <= 0 || camera.height <= 0 || target.width <= 0 || target.height <= 0)
        throw std::invalid_argument("LWIR dimensions must be positive");
    context_.original_width = camera.width;
    context_.original_height = camera.height;
    context_.input_width = target.width;
    context_.input_height = target.height;
    context_.scale = std::min(static_cast<float>(target.width) / camera.width,
        static_cast<float>(target.height) / camera.height);
    resized_size_ = cv::Size(static_cast<int>(std::round(camera.width * context_.scale)),
        static_cast<int>(std::round(camera.height * context_.scale)));
    context_.pad_x = (target.width - resized_size_.width) / 2;
    context_.pad_y = (target.height - resized_size_.height) / 2;
    // AppConfig가 YAML에서 얻은 고정 크기로 실행 경로를 한 번만 선택한다.
    if (camera.width == target.width && camera.height == target.height)
        spatial_transform_ = &LwirPreprocessor::replicate;
    else if (resized_size_ == cv::Size(camera.width, camera.height))
        spatial_transform_ = &LwirPreprocessor::pad;
    else
        spatial_transform_ = &LwirPreprocessor::resize_and_pad;
}

// 원본 영상 좌표를 보존할 수 있도록 영상과 letterbox 변환 정보를 함께 생성한다.
void LwirPreprocessor::process(
    const cv::Mat& input, cv::Mat& output, LwirPreprocessContext& context) const
{
    // Camera 계약과 다른 버퍼가 NPU 입력으로 전달되는 것을 초기에 차단한다.
    if (input.empty()) throw std::invalid_argument("LWIR input is empty");
    if (input.type() != CV_16UC1) throw std::invalid_argument("LWIR input must be CV_16UC1");
    if (input.cols != config_.camera_input.width || input.rows != config_.camera_input.height)
        throw std::invalid_argument("LWIR input dimensions do not match camera_input configuration");
    if (input.step < static_cast<std::size_t>(input.cols) * sizeof(std::uint16_t))
        throw std::invalid_argument("LWIR input stride is smaller than one image row");

    // 설정 범위를 0~255로 선형 변환한다. 범위 밖 값은 OpenCV가 포화 처리한다.
    const double range = static_cast<double>(config_.camera_input.clip_max - config_.camera_input.clip_min);
    cv::Mat gray8;
    input.convertTo(gray8, CV_8UC1, 255.0 / range,
                    -static_cast<double>(config_.camera_input.clip_min) * 255.0 / range);

    (this->*spatial_transform_)(gray8, output);
    context = context_;
}

void LwirPreprocessor::replicate(const cv::Mat& gray, cv::Mat& output) const
{
    cv::cvtColor(gray, output, cv::COLOR_GRAY2RGB);
}

void LwirPreprocessor::resize_and_pad(const cv::Mat& gray, cv::Mat& output) const
{
    cv::Mat resized;
    cv::resize(gray, resized, resized_size_, 0.0, 0.0, cv::INTER_LINEAR);
    pad(resized, output);
}

void LwirPreprocessor::pad(const cv::Mat& gray, cv::Mat& output) const
{
    const int top = context_.pad_y;
    const int left = context_.pad_x;
    const int resized_width = resized_size_.width;
    const int resized_height = resized_size_.height;
    output.create(config_.input.height, config_.input.width, CV_8UC3);
    const cv::Scalar pad_color = cv::Scalar::all(config_.preprocess.pad_value);
    if (top > 0)
        output.rowRange(0, top).setTo(pad_color);
    const int bottom = top + resized_height;
    if (bottom < output.rows)
        output.rowRange(bottom, output.rows).setTo(pad_color);

    for (int y = 0; y < resized_height; ++y)
    {
        cv::Mat output_row = output.row(y + top);
        if (left > 0)
            output_row.colRange(0, left).setTo(pad_color);
        const int right = left + resized_width;
        if (right < output.cols)
            output_row.colRange(right, output.cols).setTo(pad_color);

        const auto* src = gray.ptr<std::uint8_t>(y);
        auto* dst = output.ptr<cv::Vec3b>(y + top) + left;
        for (int x = 0; x < resized_width; ++x)
            dst[x] = cv::Vec3b(src[x], src[x], src[x]);
    }
}
