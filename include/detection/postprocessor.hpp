#pragma once

#include <string>
#include <vector>

#include "detection/i_postprocessor.hpp"
#include "detection/lwir_preprocessor.hpp"

// YOLOv8 출력 tensor를 runtime 공통 Detection 타입으로 직접 변환한다.
class Postprocessor : public IPostprocessor
{
public:
    Postprocessor(const ModelConfig& config, bool ort_configured);

    std::vector<Detection> process(
        const dxrt::TensorPtrs& outputs,
        const PreprocessContext& context) const override;

private:
    struct Candidate
    {
        float x1 = 0.0F;
        float y1 = 0.0F;
        float x2 = 0.0F;
        float y2 = 0.0F;
        float confidence = 0.0F;
        int class_id = -1;
    };

    std::vector<Candidate> decode_ort(const dxrt::TensorPtrs& outputs) const;
    std::vector<Candidate> decode_npu(const dxrt::TensorPtrs& outputs) const;
    std::vector<Candidate> apply_nms(std::vector<Candidate> candidates) const;
    std::vector<Detection> restore_coordinates(
        const std::vector<Candidate>& candidates,
        const PreprocessContext& context) const;

    std::string class_name(int class_id) const;

    int input_width_;
    int input_height_;
    bool ort_configured_;
};
