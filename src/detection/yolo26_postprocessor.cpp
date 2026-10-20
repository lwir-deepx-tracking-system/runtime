#include "detection/yolo26_postprocessor.hpp"
#include "detection/yolo_postprocessor_core.hpp"
#include <stdexcept>
Yolo26Postprocessor::Yolo26Postprocessor(const ModelConfig& c) : IPostprocessor(c), core_(std::make_unique<YoloPostprocessorCore>(c)) {
 if (c.postprocess.type != "yolo26") throw std::invalid_argument("postprocess.type does not match Yolo26Postprocessor");
}
Yolo26Postprocessor::~Yolo26Postprocessor() = default;
std::vector<Detection> Yolo26Postprocessor::process(const dxrt::TensorPtrs& o, const PreprocessContext& c) const { return core_->process(o,c); }
