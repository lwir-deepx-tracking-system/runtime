#pragma once

#include <memory>
#include "config/app_config.hpp"
#include "reid/i_reid_model.hpp"
#include "reid/i_reid_pipeline.hpp"
#include "reid/i_reid_postprocessor.hpp"
#include "reid/i_reid_preprocessor.hpp"

class LwirReidPreprocessor final : public IReidPreprocessor
{
public:
    LwirReidPreprocessor(const ReidModelConfig& model, const CameraInputConfig& camera);
    cv::Mat process(const cv::Mat& image, const cv::Rect& crop) const override;

private:
    ReidModelConfig model_;
    CameraInputConfig camera_;
};

class L2ReidPostprocessor final : public IReidPostprocessor
{
public:
    explicit L2ReidPostprocessor(int dimensions);
    std::vector<float> process(std::vector<float> embedding) const override;

private:
    int dimensions_;
};

class ReidPipeline final : public IReidPipeline
{
public:
    ReidPipeline(std::unique_ptr<IReidPreprocessor> preprocessor,
        std::unique_ptr<IReidModel> model,
        std::unique_ptr<IReidPostprocessor> postprocessor);
    std::vector<float> embed(const cv::Mat& image, const cv::Rect& crop) override;

private:
    std::unique_ptr<IReidPreprocessor> preprocessor_;
    std::unique_ptr<IReidModel> model_;
    std::unique_ptr<IReidPostprocessor> postprocessor_;
};
