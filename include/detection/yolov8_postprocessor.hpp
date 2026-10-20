#pragma once
#include <memory>
#include "detection/i_postprocessor.hpp"
class YoloPostprocessorCore;
class YoloV8Postprocessor final : public IPostprocessor {
public:
 explicit YoloV8Postprocessor(const ModelConfig&);
 ~YoloV8Postprocessor() override;
 std::vector<Detection> process(const dxrt::TensorPtrs&, const PreprocessContext&) const override;
private: std::unique_ptr<YoloPostprocessorCore> core_;
};
