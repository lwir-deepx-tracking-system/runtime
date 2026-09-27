#include "detection/lwir_preprocessor.hpp"

#include <cmath>
#include <stdexcept>

int main()
{
    ModelConfig config = load_model_config("config/model/yolov8n.yaml");
    LwirPreprocessor preprocessor(config);
    bool rejected = false;
    try {
        cv::Mat bad(480, 640, CV_8UC1);
        cv::Mat output;
        LwirPreprocessContext context;
        preprocessor.process(bad, output, context);
    } catch (const std::invalid_argument&) { rejected = true; }
    if (!rejected) return 1;

    cv::Mat input(480, 640, CV_16UC1, cv::Scalar(0));
    input.at<std::uint16_t>(0, 0) = 65535;
    cv::Mat output;
    LwirPreprocessContext context;
    preprocessor.process(input, output, context);
    if (output.type() != CV_8UC3 || output.cols != 640 || output.rows != 640) return 2;
    if (context.original_width != 640 || context.original_height != 480 ||
        context.input_width != 640 || context.input_height != 640 ||
        context.pad_x != 0 || context.pad_y != 80 || std::fabs(context.scale - 1.0F) > 1e-6F) return 3;
    if (output.at<cv::Vec3b>(80, 0) != cv::Vec3b(255, 255, 255)) return 4;
    if (output.at<cv::Vec3b>(81, 0) != cv::Vec3b(0, 0, 0)) return 5;
    if (output.at<cv::Vec3b>(0, 0) != cv::Vec3b(114, 114, 114)) return 6;
    return 0;
}
