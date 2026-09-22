#pragma once

#include <memory>

#include "app/app_config.hpp"
#include "preprocess/preprocessor.hpp"
#include "camera/camera.hpp"
#include "inference/inference.hpp"
#include "postprocess/postprocessor.hpp"
#include "tracking/tracker.hpp"

class ComponentFactory
{
public:
    
    // 전처리 객체 생성
    static std::unique_ptr<Preprocessor> create_preprocessor(const AppConfig& config);

    // 추론 객체 생성
    static std::unique_ptr<Inference> create_inference(const AppConfig& config);

    // 후처리 객체 생성
    static std::unique_ptr<Postprocessor> create_postprocessor(const AppConfig& config);

    // Tracking 객체 생성
    static std::unique_ptr<Tracker> create_tracker(const AppConfig& config);
};
