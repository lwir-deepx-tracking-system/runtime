#include "detection/yolov8_postprocessor.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

// 모델 입력 좌표계의 후보 박스 (x1, y1, x2, y2)
struct Candidate
{
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float score = 0.0f;
    int class_id = 0;
};

float iou(const Candidate& a, const Candidate& b)
{
    const float ix1 = std::max(a.x1, b.x1);
    const float iy1 = std::max(a.y1, b.y1);
    const float ix2 = std::min(a.x2, b.x2);
    const float iy2 = std::min(a.y2, b.y2);
    const float inter = std::max(0.0f, ix2 - ix1) * std::max(0.0f, iy2 - iy1);
    const float area_a = (a.x2 - a.x1) * (a.y2 - a.y1);
    const float area_b = (b.x2 - b.x1) * (b.y2 - b.y1);
    const float uni = area_a + area_b - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

std::string shape_to_string(const std::vector<std::int64_t>& shape)
{
    std::string s = "[";
    for (size_t i = 0; i < shape.size(); ++i)
        s += (i ? ", " : "") + std::to_string(shape[i]);
    return s + "]";
}

}  // namespace


Yolov8Postprocessor::Yolov8Postprocessor(
    const ModelPostprocessConfig& config,
    int max_detections)
    : config_(config),
      max_detections_(max_detections)
{
    if (config_.num_classes < 1)
        throw std::invalid_argument("[YOLOv8 후처리] num_classes는 1 이상이어야 합니다");
    if (config_.confidence_threshold <= 0.0f || config_.confidence_threshold > 1.0f)
        throw std::invalid_argument("[YOLOv8 후처리] confidence_threshold는 0 초과 1 이하여야 합니다");
    if (config_.nms_threshold <= 0.0f || config_.nms_threshold > 1.0f)
        throw std::invalid_argument("[YOLOv8 후처리] nms_threshold는 0 초과 1 이하여야 합니다");
    if (!config_.class_names.empty() &&
        static_cast<int>(config_.class_names.size()) != config_.num_classes)
        throw std::invalid_argument("[YOLOv8 후처리] class_names 개수가 num_classes와 다릅니다");
    if (max_detections_ < 1)
        throw std::invalid_argument("[YOLOv8 후처리] max_detections는 1 이상이어야 합니다");
}


std::vector<Detection> Yolov8Postprocessor::process(
    const OutputTensorView& output,
    const LwirPreprocessContext& context) const
{
    if (output.data == nullptr)
        throw std::invalid_argument("[YOLOv8 후처리] 출력 tensor 포인터가 비어 있습니다");
    if (context.scale <= 0.0f || context.original_width <= 0 || context.original_height <= 0)
        throw std::invalid_argument("[YOLOv8 후처리] letterbox 정보(scale, 원본 크기)가 올바르지 않습니다");

    // 1. tensor 형식 확인: batch 1을 떼고 [채널, N] 또는 [N, 채널]인지 판별한다.
    std::vector<std::int64_t> dims = output.shape;
    if (dims.size() == 3)
    {
        if (dims[0] != 1)
            throw std::invalid_argument("[YOLOv8 후처리] batch 크기는 1이어야 합니다: " +
                                        shape_to_string(output.shape));
        dims.erase(dims.begin());
    }
    if (dims.size() != 2)
        throw std::invalid_argument("[YOLOv8 후처리] 출력 tensor는 2차원 또는 3차원이어야 합니다: " +
                                    shape_to_string(output.shape));

    const std::int64_t channels = 4 + config_.num_classes;
    bool channel_first = false;
    std::int64_t anchors = 0;
    if (dims[0] == channels)
    {
        channel_first = true;
        anchors = dims[1];
    }
    else if (dims[1] == channels)
    {
        anchors = dims[0];
    }
    else
    {
        throw std::invalid_argument(
            "[YOLOv8 후처리] 출력 tensor " + shape_to_string(output.shape) +
            "에 채널 " + std::to_string(channels) + "개(4 + num_classes " +
            std::to_string(config_.num_classes) + ")가 없습니다. 모델과 yolov8n.yaml의 num_classes를 확인하세요");
    }

    const float* data = output.data;
    auto at = [&](std::int64_t channel, std::int64_t anchor)
    {
        return channel_first ? data[channel * anchors + anchor]
                             : data[anchor * channels + channel];
    };

    // 2. 점수 필터: anchor마다 가장 높은 class 점수를 고른다.
    std::vector<Candidate> candidates;
    for (std::int64_t i = 0; i < anchors; ++i)
    {
        int best_class = 0;
        float best_score = at(4, i);
        for (int c = 1; c < config_.num_classes; ++c)
        {
            const float s = at(4 + c, i);
            if (s > best_score)
            {
                best_score = s;
                best_class = c;
            }
        }
        if (best_score < config_.confidence_threshold)
            continue;

        const float cx = at(0, i), cy = at(1, i), w = at(2, i), h = at(3, i);
        if (w <= 0.0f || h <= 0.0f)
            continue;

        Candidate cand;
        cand.x1 = cx - w / 2.0f;
        cand.y1 = cy - h / 2.0f;
        cand.x2 = cx + w / 2.0f;
        cand.y2 = cy + h / 2.0f;
        cand.score = best_score;
        cand.class_id = best_class;
        candidates.push_back(cand);
    }

    // 3. class별 NMS: 점수 높은 순으로 남기고, 같은 class에서 많이 겹치는 박스는 버린다.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    std::vector<Candidate> kept;
    std::vector<char> suppressed(candidates.size(), 0);
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (suppressed[i])
            continue;
        kept.push_back(candidates[i]);
        if (static_cast<int>(kept.size()) >= max_detections_)
            break;
        for (size_t j = i + 1; j < candidates.size(); ++j)
        {
            if (!suppressed[j] &&
                candidates[j].class_id == candidates[i].class_id &&
                iou(candidates[i], candidates[j]) > config_.nms_threshold)
                suppressed[j] = 1;
        }
    }

    // 4. letterbox 역변환 후 원본 영상 경계로 자르고 TLWH로 만든다.
    const float max_x = static_cast<float>(context.original_width);
    const float max_y = static_cast<float>(context.original_height);
    std::vector<Detection> detections;
    detections.reserve(kept.size());
    for (const auto& k : kept)
    {
        const float x1 = std::clamp((k.x1 - context.pad_x) / context.scale, 0.0f, max_x);
        const float y1 = std::clamp((k.y1 - context.pad_y) / context.scale, 0.0f, max_y);
        const float x2 = std::clamp((k.x2 - context.pad_x) / context.scale, 0.0f, max_x);
        const float y2 = std::clamp((k.y2 - context.pad_y) / context.scale, 0.0f, max_y);
        if (x2 - x1 < 1.0f || y2 - y1 < 1.0f)
            continue;  // padding 영역에만 걸친 박스

        Detection d;
        d.x = x1;
        d.y = y1;
        d.width = x2 - x1;
        d.height = y2 - y1;
        d.class_id = k.class_id;
        d.confidence = k.score;
        if (k.class_id < static_cast<int>(config_.class_names.size()))
            d.class_name = config_.class_names[k.class_id];
        detections.push_back(std::move(d));
    }
    return detections;
}
