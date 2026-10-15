#pragma once
#include <memory>
#include "detection/i_postprocessor.hpp"
class YoloPostprocessorCore;
class Yolo26Postprocessor final : public IPostprocessor {
public:
 explicit Yolo26Postprocessor(const ModelConfig&);
 ~Yolo26Postprocessor() override;
 std::vector<Detection> process(const dxrt::TensorPtrs&, const PreprocessContext&) const override;
private: std::unique_ptr<YoloPostprocessorCore> core_;
};
