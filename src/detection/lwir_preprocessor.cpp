#include "detection/lwir_preprocessor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <opencv2/imgproc.hpp>

// 실행 중 YAML을 다시 읽지 않고 AppConfig가 검증한 값을 복사해 사용한다.
LwirPreprocessor::LwirPreprocessor(const ModelConfig& config) : IPreprocessor(config) {}

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

    // YOLO 입력 채널 수에 맞춰 단일 LWIR 채널을 RGB 세 채널로 복제한다.
    cv::Mat rgb;
    cv::cvtColor(gray8, rgb, cv::COLOR_GRAY2RGB);

    // 종횡비를 유지하는 최대 배율과 양쪽 padding 크기를 계산한다.
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

    // 후처리가 detection box를 원본 640x480 좌표로 복원할 때 사용한다.
    context.original_width = input.cols;
    context.original_height = input.rows;
    context.input_width = output.cols;
    context.input_height = output.rows;
    context.pad_x = left;
    context.pad_y = top;
    context.scale = scale;
}
