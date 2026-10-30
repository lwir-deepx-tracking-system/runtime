#include "detection/yolov8_postprocessor.hpp"
#include "detection/yolo_postprocessor_core.hpp"
#include <stdexcept>
YoloV8Postprocessor::YoloV8Postprocessor(const ModelConfig& c) : IPostprocessor(c), core_(std::make_unique<YoloPostprocessorCore>(c)) {
 if (c.postprocess.type != "yolov8") throw std::invalid_argument("postprocess.type does not match YoloV8Postprocessor");
}
YoloV8Postprocessor::~YoloV8Postprocessor() = default;
std::vector<Detection> YoloV8Postprocessor::process(const dxrt::TensorPtrs& o, const PreprocessContext& c) const { return core_->process(o,c); }
