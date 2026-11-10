#include "reid/reid_pipeline.hpp"

#include <cmath>
#include <stdexcept>

int main()
{
    ReidModelConfig model;
    model.input_width = 2;
    model.input_height = 2;
    model.input_channels = 3;
    model.resize = "stretch";
    model.channel_mode = "replicate_gray_to_rgb";
    CameraInputConfig camera;
    camera.opencv_type = "CV_16UC1";
    camera.width = 2;
    camera.height = 2;
    camera.clip_min = 100;
    camera.clip_max = 200;

    const LwirReidPreprocessor preprocessor(model, camera);
    cv::Mat image(2, 2, CV_16UC1);
    image.at<std::uint16_t>(0, 0) = 100;
    image.at<std::uint16_t>(0, 1) = 200;
    image.at<std::uint16_t>(1, 0) = 150;
    image.at<std::uint16_t>(1, 1) = 250;
    const cv::Mat input = preprocessor.process(image, cv::Rect(0, 0, 2, 2));
    if (input.type() != CV_8UC3 || input.cols != 2 || input.rows != 2 ||
        input.at<cv::Vec3b>(0, 0) != cv::Vec3b(0, 0, 0) ||
        input.at<cv::Vec3b>(0, 1) != cv::Vec3b(255, 255, 255) ||
        input.at<cv::Vec3b>(1, 1) != cv::Vec3b(255, 255, 255))
        return 1;

    try
    {
        (void)preprocessor.process(image, cv::Rect(1, 1, 2, 2));
        return 2;
    }
    catch (const std::invalid_argument&) {}

    const L2ReidPostprocessor postprocessor(2);
    const auto normalized = postprocessor.process({3.0F, 4.0F});
    if (std::abs(normalized[0] - 0.6F) > 1e-6F ||
        std::abs(normalized[1] - 0.8F) > 1e-6F)
        return 3;
    return 0;
}
