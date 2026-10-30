#include "detection/yolo_postprocessor_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
const float* require_float_data(const dxrt::TensorPtr& tensor, const char* role)
{
    if (!tensor)
        throw std::invalid_argument(std::string("Postprocess ") + role + " tensor is null");
    if (tensor->type() != dxrt::DataType::FLOAT)
        throw std::runtime_error(std::string("Postprocess ") + role + " tensor must be FLOAT");

    const void* data = tensor->data();
    if (!data)
        throw std::runtime_error(std::string("Postprocess ") + role + " tensor has no data");
    return static_cast<const float*>(data);
}

std::size_t checked_element_count(const std::vector<int64_t>& shape)
{
    if (shape.empty())
        throw std::runtime_error("Postprocess output tensor has an empty shape");

    std::size_t count = 1;
    for (const int64_t dimension : shape)
    {
        if (dimension <= 0)
            throw std::runtime_error("Postprocess output tensor dimensions must be positive");
        const auto value = static_cast<std::size_t>(dimension);
        if (count > std::numeric_limits<std::size_t>::max() / value)
            throw std::runtime_error("Postprocess output tensor shape is too large");
        count *= value;
    }
    return count;
}

void validate_float_tensor(const dxrt::TensorPtr& tensor, const char* role)
{
    (void)require_float_data(tensor, role);
    const std::size_t element_count = checked_element_count(tensor->shape());
    if (element_count > std::numeric_limits<std::size_t>::max() / sizeof(float))
        throw std::runtime_error("Postprocess output tensor byte size is too large");
    const std::size_t expected_bytes = element_count * sizeof(float);
    if (tensor->size_in_bytes() < expected_bytes)
        throw std::runtime_error(
            std::string("Postprocess ") + role +
            " tensor buffer is smaller than its shape");
}

std::string shape_string(const std::vector<int64_t>& shape)
{
    std::ostringstream stream;
    stream << '[';
    for (std::size_t i = 0; i < shape.size(); ++i)
    {
        if (i > 0) stream << ", ";
        stream << shape[i];
    }
    stream << ']';
    return stream.str();
}

std::string output_shapes_string(const dxrt::TensorPtrs& outputs)
{
    std::ostringstream stream;
    for (std::size_t i = 0; i < outputs.size(); ++i)
    {
        if (i > 0) stream << ", ";
        stream << (outputs[i] ? shape_string(outputs[i]->shape()) : "null");
    }
    return stream.str();
}

int checked_int_dimension(int64_t value, const char* role)
{
    if (value <= 0 || value > std::numeric_limits<int>::max())
        throw std::runtime_error(
            std::string("Postprocess ") + role +
            " dimension is outside the supported integer range");
    return static_cast<int>(value);
}

bool is_decoded_xywh_shape(const dxrt::TensorPtr& tensor, int num_classes)
{
    return tensor && tensor->shape().size() == 3 && tensor->shape()[0] == 1 &&
        tensor->shape()[1] == static_cast<int64_t>(4 + num_classes) &&
        tensor->shape()[2] > 0;
}

bool is_end_to_end_xyxy_shape(const dxrt::TensorPtr& tensor)
{
    return tensor && tensor->shape().size() == 3 && tensor->shape()[0] == 1 &&
        tensor->shape()[1] > 0 && tensor->shape()[2] == 6;
}

template <typename Predicate>
const dxrt::TensorPtr& select_unique_tensor(
    const dxrt::TensorPtrs& outputs,
    Predicate predicate,
    const char* description)
{
    const dxrt::TensorPtr* selected = nullptr;
    for (const auto& output : outputs)
    {
        if (!predicate(output)) continue;
        if (selected)
            throw std::runtime_error(
                std::string("Multiple tensors match ") + description);
        selected = &output;
    }
    if (!selected)
        throw std::runtime_error(
            std::string("No tensor matches ") + description +
            "; actual shapes: " + output_shapes_string(outputs));
    return *selected;
}

float sigmoid_probability(float logit)
{
    if (!std::isfinite(logit))
        throw std::runtime_error("Postprocess class logit must be finite");
    if (logit >= 0.0F)
        return 1.0F / (1.0F + std::exp(-logit));

    const float exp_logit = std::exp(logit);
    return exp_logit / (1.0F + exp_logit);
}

float probability_from_score(
    float value,
    PostprocessScoreActivation activation)
{
    if (!std::isfinite(value))
        throw std::runtime_error("Postprocess class score must be finite");
    if (activation == PostprocessScoreActivation::Sigmoid)
        return sigmoid_probability(value);
    constexpr float epsilon = 1e-5F;
    if (value < -epsilon || value > 1.0F + epsilon)
    {
        std::ostringstream message;
        message.precision(std::numeric_limits<float>::max_digits10);
        message << "Postprocess probability must be in [" << -epsilon
                << ", " << 1.0F + epsilon
                << "] when activation is none; actual value: " << value;
        throw std::runtime_error(message.str());
    }
    return std::clamp(value, 0.0F, 1.0F);
}

float split_distance(
    const float* regression,
    std::size_t grid_size,
    int side,
    std::size_t cell,
    int bins)
{
    const std::size_t offset =
        static_cast<std::size_t>(side * bins) * grid_size + cell;
    if (bins == 1)
    {
        const float distance = regression[offset];
        if (!std::isfinite(distance))
            throw std::runtime_error("Postprocess direct regression value must be finite");
        return distance;
    }

    float maximum = -std::numeric_limits<float>::infinity();
    for (int bin = 0; bin < bins; ++bin)
    {
        const float value = regression[
            offset + static_cast<std::size_t>(bin) * grid_size];
        if (!std::isfinite(value))
            throw std::runtime_error("Postprocess DFL value must be finite");
        maximum = std::max(maximum, value);
    }

    float exp_sum = 0.0F;
    float weighted_sum = 0.0F;
    for (int bin = 0; bin < bins; ++bin)
    {
        const float value = std::exp(
            regression[offset + static_cast<std::size_t>(bin) * grid_size] -
            maximum);
        exp_sum += value;
        weighted_sum += value * static_cast<float>(bin);
    }
    if (!std::isfinite(exp_sum) || exp_sum <= 0.0F)
        throw std::runtime_error(
            "Postprocess DFL distribution has invalid probability mass");
    return weighted_sum / exp_sum;
}

void validate_box(float x1, float y1, float x2, float y2)
{
    if (!std::isfinite(x1) || !std::isfinite(y1) ||
        !std::isfinite(x2) || !std::isfinite(y2))
        throw std::runtime_error("Postprocess bounding box values must be finite");
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
}  // namespace

YoloPostprocessorCore::YoloPostprocessorCore(const ModelConfig& config)
    : IPostprocessor(config),
      input_width_(config.input.width),
      input_height_(config.input.height)
{
    if (input_width_ <= 0 || input_height_ <= 0)
        throw std::invalid_argument("Postprocess input dimensions must be positive");
    if (config_.postprocess.num_classes <= 0)
        throw std::invalid_argument("Postprocess num_classes must be positive");
    if (config_.postprocess.split_output_levels <= 0 ||
        config_.postprocess.split_output_levels > 16 ||
        config_.postprocess.split_regression_bins <= 0 ||
        config_.postprocess.split_regression_bins > 256)
        throw std::invalid_argument(
            "Postprocess split output levels or regression bins are out of range");
    if (!std::isfinite(config_.postprocess.confidence_threshold) ||
        config_.postprocess.confidence_threshold < 0.0F ||
        config_.postprocess.confidence_threshold > 1.0F)
        throw std::invalid_argument(
            "Postprocess confidence threshold must be in [0, 1]");
    if (!std::isfinite(config_.postprocess.nms_threshold) ||
        config_.postprocess.nms_threshold < 0.0F ||
        config_.postprocess.nms_threshold > 1.0F)
        throw std::invalid_argument("Postprocess NMS threshold must be in [0, 1]");
    if (config_.postprocess.class_names.size() !=
        static_cast<std::size_t>(config_.postprocess.num_classes))
        throw std::invalid_argument(
            "Postprocess class_names size must match num_classes");
}

std::vector<Detection> YoloPostprocessorCore::process(
    const dxrt::TensorPtrs& outputs,
    const PreprocessContext& context) const
{
    if (context.original_width <= 0 || context.original_height <= 0 ||
        context.input_width != input_width_ || context.input_height != input_height_ ||
        context.pad_x < 0 || context.pad_y < 0 ||
        !std::isfinite(context.scale) || context.scale <= 0.0F)
        throw std::invalid_argument("Invalid postprocess preprocessing context");

    const PostprocessOutputFormat format = resolve_output_format(outputs);
    std::vector<Candidate> candidates;
    switch (format)
    {
    case PostprocessOutputFormat::DecodedXywh:
        candidates = decode_decoded_xywh(outputs);
        break;
    case PostprocessOutputFormat::EndToEndXyxy:
        candidates = decode_end_to_end_xyxy(outputs);
        break;
    case PostprocessOutputFormat::SplitHead:
        candidates = decode_split_head(outputs);
        break;
    case PostprocessOutputFormat::Auto:
        throw std::logic_error("Postprocess auto output format was not resolved");
    }

    if (should_apply_nms(format))
        candidates = apply_nms(std::move(candidates));
    return restore_coordinates(candidates, context);
}

PostprocessOutputFormat YoloPostprocessorCore::resolve_output_format(
    const dxrt::TensorPtrs& outputs) const
{
    if (config_.postprocess.output_format != PostprocessOutputFormat::Auto)
        return config_.postprocess.output_format;

    int decoded_matches = 0;
    int end_to_end_matches = 0;
    bool all_rank_four = !outputs.empty();
    for (const auto& output : outputs)
    {
        if (is_decoded_xywh_shape(output, config_.postprocess.num_classes))
            ++decoded_matches;
        if (is_end_to_end_xyxy_shape(output))
            ++end_to_end_matches;
        if (!output || output->shape().size() != 4 || output->shape()[0] != 1)
            all_rank_four = false;
    }

    const int expected_split_tensors =
        config_.postprocess.split_output_levels * 2;
    const bool split_match = all_rank_four &&
        outputs.size() == static_cast<std::size_t>(expected_split_tensors);

    const int format_matches =
        (decoded_matches == 1 ? 1 : 0) +
        (end_to_end_matches == 1 ? 1 : 0) +
        (split_match ? 1 : 0);
    if (format_matches != 1 || decoded_matches > 1 || end_to_end_matches > 1)
        throw std::runtime_error(
            "Cannot uniquely resolve postprocess.output_format from tensor shapes: " +
            output_shapes_string(outputs));

    if (decoded_matches == 1) return PostprocessOutputFormat::DecodedXywh;
    if (end_to_end_matches == 1) return PostprocessOutputFormat::EndToEndXyxy;
    return PostprocessOutputFormat::SplitHead;
}

std::vector<YoloPostprocessorCore::Candidate> YoloPostprocessorCore::decode_decoded_xywh(
    const dxrt::TensorPtrs& outputs) const
{
    const auto& tensor = select_unique_tensor(
        outputs,
        [this](const dxrt::TensorPtr& output) {
            return is_decoded_xywh_shape(
                output, config_.postprocess.num_classes);
        },
        "decoded_xywh [1, 4 + num_classes, N]");
    validate_float_tensor(tensor, "decoded_xywh");

    const int count = checked_int_dimension(
        tensor->shape()[2], "decoded candidate");
    const float* data = require_float_data(tensor, "decoded_xywh");
    std::vector<Candidate> candidates;
    for (int index = 0; index < count; ++index)
    {
        int best_class = -1;
        float best_confidence = config_.postprocess.confidence_threshold;
        for (int class_id = 0; class_id < config_.postprocess.num_classes; ++class_id)
        {
            const float score = probability_from_score(
                data[(4 + class_id) * count + index],
                PostprocessScoreActivation::None);
            if (score > best_confidence)
            {
                best_confidence = score;
                best_class = class_id;
            }
        }
        if (best_class < 0) continue;

        const float center_x = data[index];
        const float center_y = data[count + index];
        const float width = data[2 * count + index];
        const float height = data[3 * count + index];
        const float x1 = center_x - width * 0.5F;
        const float y1 = center_y - height * 0.5F;
        const float x2 = center_x + width * 0.5F;
        const float y2 = center_y + height * 0.5F;
        validate_box(x1, y1, x2, y2);
        if (x2 <= x1 || y2 <= y1) continue;
        candidates.push_back({x1, y1, x2, y2, best_confidence, best_class});
    }
    return candidates;
}

std::vector<YoloPostprocessorCore::Candidate> YoloPostprocessorCore::decode_end_to_end_xyxy(
    const dxrt::TensorPtrs& outputs) const
{
    const auto& tensor = select_unique_tensor(
        outputs,
        [](const dxrt::TensorPtr& output) {
            return is_end_to_end_xyxy_shape(output);
        },
        "end_to_end_xyxy [1, N, 6]");
    validate_float_tensor(tensor, "end_to_end_xyxy");

    const int count = checked_int_dimension(
        tensor->shape()[1], "end-to-end candidate");
    const float* data = require_float_data(tensor, "end_to_end_xyxy");
    std::vector<Candidate> candidates;
    candidates.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        const float* row = data + index * 6;
        const float confidence = probability_from_score(
            row[4], PostprocessScoreActivation::None);
        if (confidence <= config_.postprocess.confidence_threshold) continue;

        if (!std::isfinite(row[5]))
            throw std::runtime_error("Postprocess class ID must be finite");
        const float rounded_class_id = std::round(row[5]);
        if (std::fabs(row[5] - rounded_class_id) > 1e-3F)
            throw std::runtime_error("Postprocess class ID must be an integer");
        if (rounded_class_id < 0.0F ||
            rounded_class_id >=
                static_cast<float>(config_.postprocess.num_classes))
            throw std::runtime_error("Postprocess class ID is outside configured classes");
        const int class_id = static_cast<int>(rounded_class_id);

        validate_box(row[0], row[1], row[2], row[3]);
        if (row[2] <= row[0] || row[3] <= row[1]) continue;
        candidates.push_back({
            row[0], row[1], row[2], row[3], confidence, class_id});
    }
    return candidates;
}

std::vector<YoloPostprocessorCore::Candidate> YoloPostprocessorCore::decode_split_head(
    const dxrt::TensorPtrs& outputs) const
{
    const int levels = config_.postprocess.split_output_levels;
    if (outputs.size() != static_cast<std::size_t>(levels * 2))
        throw std::runtime_error(
            "Split-head output tensor count does not match split_output_levels");

    const int regression_channels =
        4 * config_.postprocess.split_regression_bins;
    if (regression_channels == config_.postprocess.num_classes)
        throw std::runtime_error(
            "Split-head regression and class channel counts are ambiguous");

    std::vector<dxrt::TensorPtr> regression_tensors;
    std::vector<dxrt::TensorPtr> class_tensors;
    for (const auto& output : outputs)
    {
        if (!output || output->shape().size() != 4 || output->shape()[0] != 1)
            throw std::runtime_error(
                "Split-head output tensors must have shape [1, C, H, W]");
        validate_float_tensor(output, "split-head");

        if (output->shape()[1] == regression_channels)
            regression_tensors.push_back(output);
        else if (output->shape()[1] == config_.postprocess.num_classes)
            class_tensors.push_back(output);
        else
            throw std::runtime_error(
                "Split-head tensor has unexpected channel count: " +
                shape_string(output->shape()));
    }

    if (regression_tensors.size() != static_cast<std::size_t>(levels) ||
        class_tensors.size() != static_cast<std::size_t>(levels))
        throw std::runtime_error(
            "Split-head regression/class tensor counts do not match configuration");

    const auto by_grid = [](const dxrt::TensorPtr& left, const dxrt::TensorPtr& right) {
        if (left->shape()[2] != right->shape()[2])
            return left->shape()[2] > right->shape()[2];
        return left->shape()[3] > right->shape()[3];
    };
    std::sort(regression_tensors.begin(), regression_tensors.end(), by_grid);
    std::sort(class_tensors.begin(), class_tensors.end(), by_grid);

    std::vector<Candidate> candidates;
    for (int level = 0; level < levels; ++level)
    {
        const auto& regression = regression_tensors[static_cast<std::size_t>(level)];
        const auto& classes = class_tensors[static_cast<std::size_t>(level)];
        if (regression->shape()[2] != classes->shape()[2] ||
            regression->shape()[3] != classes->shape()[3])
            throw std::runtime_error(
                "Split-head regression/class tensor grids do not match");

        const int height = checked_int_dimension(
            classes->shape()[2], "split-head height");
        const int width = checked_int_dimension(
            classes->shape()[3], "split-head width");
        if (input_width_ % width != 0 || input_height_ % height != 0)
            throw std::runtime_error(
                "Split-head grid does not evenly divide the model input");
        const int stride_x = input_width_ / width;
        const int stride_y = input_height_ / height;
        if (stride_x != stride_y)
            throw std::runtime_error("Split-head grid must use a uniform stride");

        const std::size_t grid_size =
            static_cast<std::size_t>(height) * static_cast<std::size_t>(width);
        const float* regression_data =
            require_float_data(regression, "split regression");
        const float* class_data = require_float_data(classes, "split class");
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t cell =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x);
                int best_class = -1;
                float best_confidence = config_.postprocess.confidence_threshold;
                for (int class_id = 0;
                     class_id < config_.postprocess.num_classes;
                     ++class_id)
                {
                    const float confidence = probability_from_score(
                        class_data[
                            static_cast<std::size_t>(class_id) * grid_size + cell],
                        config_.postprocess.split_class_score_activation);
                    if (confidence > best_confidence)
                    {
                        best_confidence = confidence;
                        best_class = class_id;
                    }
                }
                if (best_class < 0) continue;

                const int bins = config_.postprocess.split_regression_bins;
                const float left = split_distance(
                    regression_data, grid_size, 0, cell, bins);
                const float top = split_distance(
                    regression_data, grid_size, 1, cell, bins);
                const float right = split_distance(
                    regression_data, grid_size, 2, cell, bins);
                const float bottom = split_distance(
                    regression_data, grid_size, 3, cell, bins);
                const float anchor_x = static_cast<float>(x) + 0.5F;
                const float anchor_y = static_cast<float>(y) + 0.5F;
                const float x1 = (anchor_x - left) * stride_x;
                const float y1 = (anchor_y - top) * stride_y;
                const float x2 = (anchor_x + right) * stride_x;
                const float y2 = (anchor_y + bottom) * stride_y;
                validate_box(x1, y1, x2, y2);
                if (x2 <= x1 || y2 <= y1) continue;
                candidates.push_back({
                    x1, y1, x2, y2, best_confidence, best_class});
            }
        }
    }
    return candidates;
}

bool YoloPostprocessorCore::should_apply_nms(PostprocessOutputFormat format) const
{
    switch (config_.postprocess.nms_mode)
    {
    case PostprocessNmsMode::Enabled:
        return true;
    case PostprocessNmsMode::Disabled:
        return false;
    case PostprocessNmsMode::Auto:
        return format != PostprocessOutputFormat::EndToEndXyxy;
    }
    throw std::logic_error("Unknown postprocess NMS mode");
}

std::vector<YoloPostprocessorCore::Candidate> YoloPostprocessorCore::apply_nms(
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
        if (suppressed[i]) continue;
        kept.push_back(candidates[i]);
        for (std::size_t j = i + 1; j < candidates.size(); ++j)
        {
            const bool same_nms_group = config_.postprocess.class_agnostic_nms ||
                candidates[i].class_id == candidates[j].class_id;
            if (!suppressed[j] && same_nms_group &&
                box_iou(candidates[i], candidates[j]) >=
                    config_.postprocess.nms_threshold)
                suppressed[j] = true;
        }
    }
    return kept;
}

std::vector<Detection> YoloPostprocessorCore::restore_coordinates(
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
        if (x2 <= x1 || y2 <= y1) continue;

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

std::string YoloPostprocessorCore::class_name(int class_id) const
{
    if (class_id >= 0 &&
        static_cast<std::size_t>(class_id) <
            config_.postprocess.class_names.size())
        return config_.postprocess.class_names[static_cast<std::size_t>(class_id)];
    return std::to_string(class_id);
}
