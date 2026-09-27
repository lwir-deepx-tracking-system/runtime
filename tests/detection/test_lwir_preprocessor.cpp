#include "detection/lwir_preprocessor.hpp"

#include <cmath>
#include <stdexcept>

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
    return 0;
}
