#pragma once

#include "app/app_config.hpp"

#include <opencv2/core.hpp>

// letterbox 결과를 원본 영상 좌표로 되돌릴 때 필요한 변환 정보.
struct LwirPreprocessContext
{
    int original_width = 0;
    int original_height = 0;
    int input_width = 0;
    int input_height = 0;
    int pad_x = 0;
    int pad_y = 0;
    float scale = 1.0F;
};

// CV_16UC1 LWIR 영상을 DEEPX YOLO 입력용 CV_8UC3 tensor 영상으로 변환한다.
class LwirPreprocessor
{
public:
    explicit LwirPreprocessor(const ModelConfig& config);

    // 입력을 clipping·8-bit 변환·RGB 복제·letterbox하고 좌표 변환값을 기록한다.
    void process(const cv::Mat& input, cv::Mat& output, LwirPreprocessContext& context) const;

    // runner가 기대하는 최종 tensor 크기를 노출한다.
    int input_width() const noexcept { return config_.input.width; }
    int input_height() const noexcept { return config_.input.height; }

private:
    // YAML 파싱은 AppConfig가 담당하고 전처리기는 검증된 값만 보관한다.
    ModelConfig config_;
};
