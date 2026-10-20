#pragma once

#include <vector>

#include <dxrt/dxrt_cxx_api.h>

#include "common/detection.hpp"
#include "detection/i_preprocessor.hpp"

// 모델 출력 tensor를 runtime 공통 Detection 결과로 변환하는 인터페이스.
class IPostprocessor
{
public:
    explicit IPostprocessor(const ModelConfig& config) : config_(config) {}
    virtual ~IPostprocessor() = default;

    virtual std::vector<Detection> process(
        const dxrt::TensorPtrs& outputs,
        const PreprocessContext& context) const = 0;

protected:
    ModelConfig config_;
};
