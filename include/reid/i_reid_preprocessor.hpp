#pragma once

#include <opencv2/core.hpp>

class IReidPreprocessor
{
public:
    virtual ~IReidPreprocessor() = default;
    virtual cv::Mat process(const cv::Mat& image, const cv::Rect& crop) const = 0;
};
