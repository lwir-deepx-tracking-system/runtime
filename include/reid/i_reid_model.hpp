#pragma once

#include <vector>
#include <opencv2/core.hpp>

// 모델 입력 텐서를 받아 정규화 전 embedding을 반환한다.
class IReidModel
{
public:
    virtual ~IReidModel() = default;
    virtual std::vector<float> infer(const cv::Mat& input) = 0;
};
