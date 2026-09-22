#pragma once

#include <string>

#include "preprocess/preprocessor.hpp"

class YoloV8Preprocessor : public Preprocessor
{
private:
    std::string config_path_;

    int input_width_;
    int input_height_;

    std::string resize_method_;
    bool normalize_;

    // 모델 YAML을 한 번 읽어서 전처리 설정 저장
    void load_config();

public:
    explicit YoloV8Preprocessor(std::string config_path);

    ~YoloV8Preprocessor() override = default;

    ModelInput preprocess(const Frame& frame) override;
};