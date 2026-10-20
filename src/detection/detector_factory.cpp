#include "detection/detector_factory.hpp"
#include "detection/dxrt_detection_pipeline.hpp"
#include "detection/lwir_preprocessor.hpp"
#include "detection/yolov8_postprocessor.hpp"
#include "detection/yolo26_postprocessor.hpp"
#include <stdexcept>
std::unique_ptr<IDetectionPipeline> DetectorFactory::create(const ModelConfig& m,const DetectionConfig& d) {
 std::unique_ptr<IPreprocessor> pre;
 if(m.preprocess.type=="lwir_letterbox") pre=std::make_unique<LwirPreprocessor>(m);
 else throw std::invalid_argument("Unsupported preprocess.type: "+m.preprocess.type);
 std::unique_ptr<IPostprocessor> post;
 if(m.postprocess.type=="yolov8") post=std::make_unique<YoloV8Postprocessor>(m);
 else if(m.postprocess.type=="yolo26") post=std::make_unique<Yolo26Postprocessor>(m);
 else throw std::invalid_argument("Unsupported postprocess.type: "+m.postprocess.type);
 return std::make_unique<DxrtDetectionPipeline>(m,d,std::move(pre),std::move(post));
}
