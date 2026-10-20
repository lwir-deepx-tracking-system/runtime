#include "detection/yolo_postprocessor_core.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
dxrt::TensorPtr make_tensor(
    const std::string& name,
    std::vector<int64_t> shape,
    const std::vector<float>& values)
{
    return std::make_shared<dxrt::Tensor>(
        name,
        static_cast<int>(dxrt::DataType::FLOAT),
        std::move(shape),
        values.data(),
        values.size() * sizeof(float),
        true);
}

ModelConfig make_config()
{
    ModelConfig config;
    config.input.width = 640;
    config.input.height = 640;
    config.postprocess.output_format = PostprocessOutputFormat::Auto;
    config.postprocess.split_output_levels = 3;
    config.postprocess.split_regression_bins = 16;
    config.postprocess.split_class_score_activation =
        PostprocessScoreActivation::None;
    config.postprocess.nms_mode = PostprocessNmsMode::Auto;
    config.postprocess.class_agnostic_nms = true;
    config.postprocess.confidence_threshold = 0.25F;
    config.postprocess.nms_threshold = 0.45F;
    config.postprocess.num_classes = 2;
    config.postprocess.class_names = {"person", "car"};
    return config;
}

PreprocessContext make_context()
{
    PreprocessContext context;
    context.original_width = 640;
    context.original_height = 480;
    context.input_width = 640;
    context.input_height = 640;
    context.pad_x = 0;
    context.pad_y = 80;
    context.scale = 1.0F;
    return context;
}

bool close(float actual, float expected)
{
    return std::fabs(actual - expected) < 1e-5F;
}
}  // namespace

int main()
{
    // [1, 4+C, N]의 xywh + class score output을 원본 frame 좌표로 복원한다.
    {
        ModelConfig config = make_config();
        YoloPostprocessorCore postprocessor(config);
        const dxrt::TensorPtrs outputs = {make_tensor(
            "decoded",
            {1, 6, 1},
            {100.0F, 180.0F, 40.0F, 60.0F, 0.9F, 0.1F})};
        const auto detections = postprocessor.process(outputs, make_context());
        if (detections.size() != 1) return 1;
        const Detection& detection = detections.front();
        if (detection.class_id != 0 || detection.class_name != "person" ||
            !close(detection.confidence, 0.9F) ||
            !close(detection.x, 80.0F) || !close(detection.y, 70.0F) ||
            !close(detection.width, 40.0F) || !close(detection.height, 60.0F))
            return 2;
    }

    // [1, N, 6] end-to-end xyxy output은 class ID를 읽고 auto NMS를 생략한다.
    {
        ModelConfig config = make_config();
        YoloPostprocessorCore postprocessor(config);
        const dxrt::TensorPtrs outputs = {make_tensor(
            "end_to_end",
            {1, 2, 6},
            {
                10.0F, 90.0F, 30.0F, 130.0F, 0.8F, 1.0F,
                40.0F, 100.0F, 50.0F, 120.0F, 0.1F, 0.0F,
            })};
        const auto detections = postprocessor.process(outputs, make_context());
        if (detections.size() != 1) return 3;
        const Detection& detection = detections.front();
        if (detection.class_id != 1 || detection.class_name != "car" ||
            !close(detection.confidence, 0.8F) ||
            !close(detection.x, 10.0F) || !close(detection.y, 10.0F) ||
            !close(detection.width, 20.0F) || !close(detection.height, 40.0F))
            return 4;
    }

    // YOLO26 reg_max=1 split-head는 DFL softmax 없이 네 distance를 직접 decode한다.
    {
        ModelConfig config = make_config();
        config.input.width = 8;
        config.input.height = 8;
        config.postprocess.split_output_levels = 1;
        config.postprocess.split_regression_bins = 1;
        config.postprocess.split_class_score_activation =
            PostprocessScoreActivation::Sigmoid;
        YoloPostprocessorCore postprocessor(config);

        PreprocessContext context;
        context.original_width = 8;
        context.original_height = 8;
        context.input_width = 8;
        context.input_height = 8;
        context.scale = 1.0F;

        const dxrt::TensorPtrs outputs = {
            // use_ort=false의 class Conv output은 [0, 1] 확률이 아닌 raw logit이다.
            make_tensor("classes", {1, 2, 1, 1}, {2.0F, -2.0F}),
            make_tensor("regression", {1, 4, 1, 1},
                        {0.25F, 0.25F, 0.25F, 0.25F}),
        };
        const auto detections = postprocessor.process(outputs, context);
        if (detections.size() != 1) return 5;
        const Detection& detection = detections.front();
        if (detection.class_id != 0 ||
            !close(detection.confidence, 0.88079708F) ||
            !close(detection.x, 2.0F) || !close(detection.y, 2.0F) ||
            !close(detection.width, 4.0F) || !close(detection.height, 4.0F))
            return 6;
    }

    return 0;
}
