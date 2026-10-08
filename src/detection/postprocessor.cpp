#include "detection/postprocessor.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
constexpr int kDflBins = 16;
constexpr int kDflChannels = 4 * kDflBins;

const float* require_float_data(const dxrt::TensorPtr& tensor, const char* role)
{
    if (!tensor)
        throw std::invalid_argument(std::string("YOLOv8 ") + role + " tensor is null");
    if (tensor->type() != dxrt::DataType::FLOAT)
        throw std::runtime_error(std::string("YOLOv8 ") + role + " tensor must be FLOAT");

    const void* data = tensor->data();
    if (!data)
        throw std::runtime_error(std::string("YOLOv8 ") + role + " tensor has no data");
    return static_cast<const float*>(data);
}

std::size_t checked_element_count(const std::vector<int64_t>& shape)
{
    if (shape.empty())
        throw std::runtime_error("YOLOv8 output tensor has an empty shape");

    std::size_t count = 1;
    for (const int64_t dimension : shape)
    {
        if (dimension <= 0)
            throw std::runtime_error("YOLOv8 output tensor dimensions must be positive");
        const auto value = static_cast<std::size_t>(dimension);
        if (count > std::numeric_limits<std::size_t>::max() / value)
            throw std::runtime_error("YOLOv8 output tensor shape is too large");
        count *= value;
    }
    return count;
}

void validate_float_tensor(const dxrt::TensorPtr& tensor, const char* role)
{
    const float* data = require_float_data(tensor, role);
    (void)data;

    const std::size_t expected_bytes =
        checked_element_count(tensor->shape()) * sizeof(float);
    if (tensor->size_in_bytes() < expected_bytes)
        throw std::runtime_error(std::string("YOLOv8 ") + role + " tensor buffer is smaller than its shape");
}

template <typename Candidate>
float box_iou(const Candidate& a, const Candidate& b)
{
    const float left = std::max(a.x1, b.x1);
    const float top = std::max(a.y1, b.y1);
    const float right = std::min(a.x2, b.x2);
    const float bottom = std::min(a.y2, b.y2);
    const float intersection =
        std::max(0.0F, right - left) * std::max(0.0F, bottom - top);
    const float area_a =
        std::max(0.0F, a.x2 - a.x1) * std::max(0.0F, a.y2 - a.y1);
    const float area_b =
        std::max(0.0F, b.x2 - b.x1) * std::max(0.0F, b.y2 - b.y1);
    const float union_area = area_a + area_b - intersection;
    return union_area > 0.0F ? intersection / union_area : 0.0F;
}

float dfl_distance(const float* regression, int grid_size, int side, int cell)
{
    const int offset = side * kDflBins * grid_size + cell;
    float maximum = -std::numeric_limits<float>::infinity();
    for (int bin = 0; bin < kDflBins; ++bin)
        maximum = std::max(maximum, regression[offset + bin * grid_size]);

    float exp_sum = 0.0F;
    float weighted_sum = 0.0F;
    for (int bin = 0; bin < kDflBins; ++bin)
    {
        const float value = std::exp(
            regression[offset + bin * grid_size] - maximum);
        exp_sum += value;
        weighted_sum += value * static_cast<float>(bin);
    }
    if (exp_sum <= 0.0F)
        throw std::runtime_error("YOLOv8 DFL distribution has zero probability mass");
    return weighted_sum / exp_sum;
}

} // namespace

Postprocessor::Postprocessor(const ModelConfig& config, bool ort_configured)
    : IPostprocessor(config),
      input_width_(config.input.width),
      input_height_(config.input.height),
      ort_configured_(ort_configured)
{
    if (input_width_ <= 0 || input_height_ <= 0)
        throw std::invalid_argument("YOLOv8 input dimensions must be positive");
    if (config_.postprocess.num_classes <= 0)
        throw std::invalid_argument("YOLOv8 num_classes must be positive");
    if (config_.postprocess.confidence_threshold < 0.0F || config_.postprocess.confidence_threshold > 1.0F)
        throw std::invalid_argument("YOLOv8 confidence threshold must be in [0, 1]");
    if (config_.postprocess.nms_threshold < 0.0F || config_.postprocess.nms_threshold > 1.0F)
        throw std::invalid_argument("YOLOv8 NMS threshold must be in [0, 1]");
    if (!config_.postprocess.class_names.empty() &&
        config_.postprocess.class_names.size() != static_cast<std::size_t>(config_.postprocess.num_classes))
        throw std::invalid_argument("YOLOv8 class_names size must match num_classes");
}

std::vector<Detection> Postprocessor::process(
    const dxrt::TensorPtrs& outputs,
    const PreprocessContext& context) const
{
    if (context.original_width <= 0 || context.original_height <= 0 ||
        context.input_width != input_width_ || context.input_height != input_height_ ||
        context.pad_x < 0 || context.pad_y < 0 ||
        !std::isfinite(context.scale) || context.scale <= 0.0F)
        throw std::invalid_argument("Invalid YOLOv8 preprocessing context");

    auto candidates = ort_configured_ ? decode_ort(outputs) : decode_npu(outputs);
    candidates = apply_nms(std::move(candidates));
    return restore_coordinates(candidates, context);
}

std::vector<Postprocessor::Candidate> Postprocessor::decode_ort(
    const dxrt::TensorPtrs& outputs) const
{
    const dxrt::TensorPtr* selected = nullptr;
    for (const auto& output : outputs)
    {
        if (output && output->shape().size() == 3)
        {
            selected = &output;
            break;
        }
    }
    if (!selected)
        throw std::runtime_error("YOLOv8 ORT output must contain a rank-3 tensor");

    const auto& tensor = *selected;
    validate_float_tensor(tensor, "ORT output");
    const auto& shape = tensor->shape();
    if (shape[0] != 1 || shape[1] != static_cast<int64_t>(4 + config_.postprocess.num_classes))
    {
        std::ostringstream message;
        message << "Unexpected YOLOv8 ORT output shape; expected [1, "
                << (4 + config_.postprocess.num_classes) << ", N]";
        throw std::runtime_error(message.str());
    }

    const int count = static_cast<int>(shape[2]);
    const float* data = require_float_data(tensor, "ORT output");
    std::vector<Candidate> candidates;
    for (int index = 0; index < count; ++index)
    {
        int best_class = -1;
        float best_confidence = config_.postprocess.confidence_threshold;
        for (int class_id = 0; class_id < config_.postprocess.num_classes; ++class_id)
        {
            const float score = data[(4 + class_id) * count + index];
            if (score > best_confidence)
            {
                best_confidence = score;
                best_class = class_id;
            }
        }
        if (best_class < 0)
            continue;

        const float center_x = data[index];
        const float center_y = data[count + index];
        const float width = data[2 * count + index];
        const float height = data[3 * count + index];
        candidates.push_back({
            center_x - width * 0.5F,
            center_y - height * 0.5F,
            center_x + width * 0.5F,
            center_y + height * 0.5F,
            best_confidence,
            best_class
        });
    }
    return candidates;
}

std::vector<Postprocessor::Candidate> Postprocessor::decode_npu(
    const dxrt::TensorPtrs& outputs) const
{
    if (outputs.size() != 6)
        throw std::runtime_error("YOLOv8 NPU output must contain three regression/class tensor pairs");

    struct TensorLevel
    {
        dxrt::TensorPtr regression;
        dxrt::TensorPtr classes;
    };
    std::vector<dxrt::TensorPtr> regression_tensors;
    std::vector<dxrt::TensorPtr> class_tensors;

    for (const auto& output : outputs)
    {
        if (!output || output->shape().size() != 4 || output->shape()[0] != 1)
            throw std::runtime_error("YOLOv8 NPU output tensors must have shape [1, C, H, W]");
        validate_float_tensor(output, "NPU output");

        if (output->shape()[1] == kDflChannels)
            regression_tensors.push_back(output);
        else
            class_tensors.push_back(output);
    }

    if (config_.postprocess.num_classes == kDflChannels ||
        regression_tensors.size() != 3 || class_tensors.size() != 3)
        throw std::runtime_error("Cannot distinguish YOLOv8 DFL and class output tensors");

    const auto by_height = [](const dxrt::TensorPtr& left, const dxrt::TensorPtr& right) {
        return left->shape()[2] > right->shape()[2];
    };
    std::sort(regression_tensors.begin(), regression_tensors.end(), by_height);
    std::sort(class_tensors.begin(), class_tensors.end(), by_height);

    std::vector<TensorLevel> levels;
    levels.reserve(3);
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto& regression = regression_tensors[i];
        const auto& classes = class_tensors[i];
        if (regression->shape()[1] != kDflChannels ||
            classes->shape()[1] != config_.postprocess.num_classes ||
            regression->shape()[2] != classes->shape()[2] ||
            regression->shape()[3] != classes->shape()[3])
            throw std::runtime_error("YOLOv8 NPU regression/class tensor shapes do not match");

        levels.push_back({regression, classes});
    }

    std::vector<Candidate> candidates;
    for (const auto& level : levels)
    {
        const auto& shape = level.classes->shape();
        const int height = static_cast<int>(shape[2]);
        const int width = static_cast<int>(shape[3]);
        if (input_width_ % width != 0 || input_height_ % height != 0)
            throw std::runtime_error("YOLOv8 NPU output grid does not evenly divide the model input");
        const int stride_x = input_width_ / width;
        const int stride_y = input_height_ / height;
        if (stride_x != stride_y)
            throw std::runtime_error("YOLOv8 NPU output grid must use a uniform stride");

        const int grid_size = height * width;
        const float* regression = require_float_data(level.regression, "DFL regression");
        const float* classes = require_float_data(level.classes, "class score");
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const int cell = y * width + x;
                int best_class = -1;
                float best_confidence = config_.postprocess.confidence_threshold;
                for (int class_id = 0; class_id < config_.postprocess.num_classes; ++class_id)
                {
                    const float score = classes[class_id * grid_size + cell];
                    if (score > best_confidence)
                    {
                        best_confidence = score;
                        best_class = class_id;
                    }
                }
                if (best_class < 0)
                    continue;

                const float left = dfl_distance(regression, grid_size, 0, cell);
                const float top = dfl_distance(regression, grid_size, 1, cell);
                const float right = dfl_distance(regression, grid_size, 2, cell);
                const float bottom = dfl_distance(regression, grid_size, 3, cell);
                const float anchor_x = static_cast<float>(x) + 0.5F;
                const float anchor_y = static_cast<float>(y) + 0.5F;
                candidates.push_back({
                    (anchor_x - left) * stride_x,
                    (anchor_y - top) * stride_y,
                    (anchor_x + right) * stride_x,
                    (anchor_y + bottom) * stride_y,
                    best_confidence,
                    best_class
                });
            }
        }
    }
    return candidates;
}

std::vector<Postprocessor::Candidate> Postprocessor::apply_nms(
    std::vector<Candidate> candidates) const
{
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            return left.confidence > right.confidence;
        });

    std::vector<Candidate> kept;
    std::vector<bool> suppressed(candidates.size(), false);
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (suppressed[i])
            continue;
        kept.push_back(candidates[i]);
        for (std::size_t j = i + 1; j < candidates.size(); ++j)
        {
            // dx_app YOLOv8 postprocess의 기존 동작과 맞춰 class-agnostic NMS를 한다.
            if (!suppressed[j] && box_iou(candidates[i], candidates[j]) >= config_.postprocess.nms_threshold)
                suppressed[j] = true;
        }
    }
    return kept;
}

std::vector<Detection> Postprocessor::restore_coordinates(
    const std::vector<Candidate>& candidates,
    const PreprocessContext& context) const
{
    std::vector<Detection> detections;
    detections.reserve(candidates.size());
    for (const Candidate& candidate : candidates)
    {
        const float x1 = std::clamp(
            (candidate.x1 - static_cast<float>(context.pad_x)) / context.scale,
            0.0F, static_cast<float>(context.original_width));
        const float y1 = std::clamp(
            (candidate.y1 - static_cast<float>(context.pad_y)) / context.scale,
            0.0F, static_cast<float>(context.original_height));
        const float x2 = std::clamp(
            (candidate.x2 - static_cast<float>(context.pad_x)) / context.scale,
            0.0F, static_cast<float>(context.original_width));
        const float y2 = std::clamp(
            (candidate.y2 - static_cast<float>(context.pad_y)) / context.scale,
            0.0F, static_cast<float>(context.original_height));
        if (x2 <= x1 || y2 <= y1)
            continue;

        Detection detection;
        detection.x = x1;
        detection.y = y1;
        detection.width = x2 - x1;
        detection.height = y2 - y1;
        detection.class_id = candidate.class_id;
        detection.confidence = candidate.confidence;
        detection.class_name = class_name(candidate.class_id);
        detections.push_back(std::move(detection));
    }
    return detections;
}

std::string Postprocessor::class_name(int class_id) const
{
    if (class_id >= 0 && static_cast<std::size_t>(class_id) < config_.postprocess.class_names.size())
        return config_.postprocess.class_names[static_cast<std::size_t>(class_id)];
    return std::to_string(class_id);
}
