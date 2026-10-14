#pragma once

#include <string>
#include <vector>

#include "detection/i_postprocessor.hpp"
#include "detection/lwir_preprocessor.hpp"

// AppConfig가 검증한 model output 계약에 따라 tensor를 Detection으로 변환한다.
class Postprocessor : public IPostprocessor
{
public:
    explicit Postprocessor(const ModelConfig& config);

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

    PostprocessOutputFormat resolve_output_format(
        const dxrt::TensorPtrs& outputs) const;
    std::vector<Candidate> decode_decoded_xywh(
        const dxrt::TensorPtrs& outputs) const;
    std::vector<Candidate> decode_end_to_end_xyxy(
        const dxrt::TensorPtrs& outputs) const;
    std::vector<Candidate> decode_split_head(
        const dxrt::TensorPtrs& outputs) const;
    std::vector<Candidate> apply_nms(std::vector<Candidate> candidates) const;
    std::vector<Detection> restore_coordinates(
        const std::vector<Candidate>& candidates,
        const PreprocessContext& context) const;

    std::string class_name(int class_id) const;
    bool should_apply_nms(PostprocessOutputFormat format) const;

    int input_width_;
    int input_height_;
};
