#pragma once

#include <vector>
#include <opencv2/core.hpp>

class IReidPipeline
{
public:
    virtual ~IReidPipeline() = default;
    virtual std::vector<float> embed(const cv::Mat& image, const cv::Rect& crop) = 0;
};
