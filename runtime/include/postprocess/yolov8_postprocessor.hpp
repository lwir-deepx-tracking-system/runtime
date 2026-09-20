#pragma once

#include <string>
#include <vector>

#include "postprocess/postprocessor.hpp"


// YOLOv8 계열 모델의 Raw Output을 Detection으로 변환한다.
class YoloV8Postprocessor : public Postprocessor
{
private:
    std::string config_path_;

    float confidence_threshold_;
    float nms_threshold_;

    // Model YAML에서 후처리 설정을 읽는다.
    void load_config();

public:
    explicit YoloV8Postprocessor(std::string config_path);

    ~YoloV8Postprocessor() override = default;

    // YOLOv8 Raw Output을 Detection 목록으로 변환한다.
    std::vector<Detection> postprocess(
        const ModelOutput& output
    ) override;
};