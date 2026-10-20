#pragma once

#include "detection/i_preprocessor.hpp"

using LwirPreprocessContext = PreprocessContext;

// CV_16UC1 LWIR 영상을 DEEPX YOLO 입력용 CV_8UC3 tensor 영상으로 변환한다.
class LwirPreprocessor : public IPreprocessor
{
public:
    explicit LwirPreprocessor(const ModelConfig& config);

    // 입력을 clipping·8-bit 변환·RGB 복제·letterbox하고 좌표 변환값을 기록한다.
    void process(const cv::Mat& input, cv::Mat& output, PreprocessContext& context) const override;

    // runner가 기대하는 최종 tensor 크기를 노출한다.
    int input_width() const noexcept override { return config_.input.width; }
    int input_height() const noexcept override { return config_.input.height; }

};
