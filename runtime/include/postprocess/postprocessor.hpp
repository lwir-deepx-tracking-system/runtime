#pragma once

#include <vector>

#include "common/model_output.hpp"
#include "common/detection.hpp"


// 모델의 Raw Output Tensor를 Detection 결과로 변환하는 공통 인터페이스
class Postprocessor
{
public:
    virtual ~Postprocessor() = default;

    virtual std::vector<Detection> postprocess(
        const ModelOutput& output
    ) = 0;
};