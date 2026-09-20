#pragma once

#include <string>

#include "common/model_input.hpp"
#include "common/model_output.hpp"


// 전처리된 입력을 DEEPX NPU에 전달하고 추론 결과를 반환한다.
class Inference
{
private:
    std::string config_path_;

    std::string model_path;
    
    // Model YAML에서 설정을 읽고 모델을 초기화한다.
    void load_config();

public:
    explicit Inference(std::string config_path);

    // ModelInput으로 NPU 추론을 수행한다.
    ModelOutput run(const ModelInput& input);
};