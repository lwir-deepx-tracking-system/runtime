#pragma once

#include <opencv2/core.hpp>

#include "config/app_config.hpp"

// 전처리와 후처리가 공유하는 좌표 변환 정보.
struct PreprocessContext
{
    int original_width = 0;
    int original_height = 0;
    int input_width = 0;
    int input_height = 0;
    int pad_x = 0;
    int pad_y = 0;
    float scale = 1.0F;
};

// 모델 입력 영상을 만들고 후처리용 좌표 문맥을 제공하는 인터페이스.
class IPreprocessor
{
public:
    explicit IPreprocessor(const ModelConfig& config) : config_(config) {}
    virtual ~IPreprocessor() = default;

    virtual void process(
        const cv::Mat& input,
        cv::Mat& output,
        PreprocessContext& context) const = 0;

    virtual int input_width() const noexcept = 0;
    virtual int input_height() const noexcept = 0;

protected:
    ModelConfig config_;
};
