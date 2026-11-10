#include "detection/lwir_preprocessor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <opencv2/imgproc.hpp>

// LWIR 입력 검증, 16→8-bit 변환과 letterbox 좌표 정보를 함께 확인한다.
int main()
{
    ModelConfig config = load_model_config("config/model/yolov8n.yaml");
    LwirPreprocessor preprocessor(config);

    // Camera 계약과 다른 8-bit 입력은 처리 전에 거부해야 한다.
    bool rejected = false;
    try {
        cv::Mat bad(480, 640, CV_8UC1);
        cv::Mat output;
        LwirPreprocessContext context;
        preprocessor.process(bad, output, context);
    } catch (const std::invalid_argument&) { rejected = true; }
    if (!rejected) return 1;

    // 640x480 원본은 640x640 입력 중앙에 위아래 80px padding으로 배치된다.
    cv::Mat input(480, 640, CV_16UC1, cv::Scalar(0));
    input.at<std::uint16_t>(0, 0) = 65535;
    cv::Mat output;
    LwirPreprocessContext context;
    preprocessor.process(input, output, context);
    if (output.type() != CV_8UC3 || output.cols != 640 || output.rows != 640) return 2;
    if (context.original_width != 640 || context.original_height != 480 ||
        context.input_width != 640 || context.input_height != 640 ||
        context.pad_x != 0 || context.pad_y != 80 || std::fabs(context.scale - 1.0F) > 1e-6F) return 3;

    // 최대값과 최소값의 8-bit 변환 및 padding 색상 114를 픽셀 단위로 검증한다.
    if (output.at<cv::Vec3b>(80, 0) != cv::Vec3b(255, 255, 255)) return 4;
    if (output.at<cv::Vec3b>(81, 0) != cv::Vec3b(0, 0, 0)) return 5;
    if (output.at<cv::Vec3b>(0, 0) != cv::Vec3b(114, 114, 114)) return 6;

    // 동일 크기·padding 전용·실제 resize 경로가 기존 letterbox와 픽셀 단위로 일치한다.
    cv::Mat backing(482, 644, CV_16UC1);
    cv::Mat roi = backing(cv::Rect(1, 1, 640, 480));
    for (int y = 0; y < roi.rows; ++y)
        for (int x = 0; x < roi.cols; ++x)
            roi.at<std::uint16_t>(y, x) = static_cast<std::uint16_t>(x * 71 + y * 137);
    for (const auto size : {cv::Size(640, 480), cv::Size(640, 481),
            cv::Size(640, 640), cv::Size(320, 240), cv::Size(320, 320)}) {
        auto rectangular = config;
        rectangular.input.width = size.width;
        rectangular.input.height = size.height;
        LwirPreprocessor selected(rectangular);
        selected.process(roi, output, context);
        cv::Mat gray, resized, rgb, expected;
        roi.convertTo(gray, CV_8UC1, 255.0 / 65535.0);
        const float scale = std::min(size.width / 640.0F, size.height / 480.0F);
        const cv::Size fitted(static_cast<int>(std::round(640 * scale)),
            static_cast<int>(std::round(480 * scale)));
        cv::resize(gray, resized, fitted, 0.0, 0.0, cv::INTER_LINEAR);
        cv::cvtColor(resized, rgb, cv::COLOR_GRAY2RGB);
        const int left = (size.width - fitted.width) / 2;
        const int top = (size.height - fitted.height) / 2;
        cv::copyMakeBorder(rgb, expected, top, size.height - fitted.height - top,
            left, size.width - fitted.width - left, cv::BORDER_CONSTANT, cv::Scalar::all(114));
        if (output.size() != size || output.type() != CV_8UC3 ||
            cv::norm(output, expected, cv::NORM_INF) != 0) return 7;
        if (context.scale != scale || context.pad_x != left || context.pad_y != top ||
            context.input_width != size.width || context.input_height != size.height ||
            context.original_width != 640 || context.original_height != 480) return 8;
        // 출력 버퍼 재사용에도 이전 프레임의 픽셀이 남지 않아야 한다.
        selected.process(roi, output, context);
        if (cv::norm(output, expected, cv::NORM_INF) != 0) return 9;
    }
    return 0;
}
