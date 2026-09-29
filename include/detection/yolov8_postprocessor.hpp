#pragma once

#include <cstdint>
#include <vector>

#include "app/app_config.hpp"
#include "common/detection.hpp"
#include "detection/lwir_preprocessor.hpp"

// NPU 출력 tensor 하나를 가리키는 view.
// DX-RT 타입에 의존하지 않도록 float 포인터와 shape만 받는다. 메모리는 소유하지 않는다.
struct OutputTensorView
{
    const float* data = nullptr;
    std::vector<std::int64_t> shape;
};

// YOLOv8 출력 tensor를 Detection 목록으로 바꾼다.
//
// 입력 tensor 형식 (둘 다 자동 인식):
//   [1, 4 + num_classes, N]  (Ultralytics 기본 export, 채널 우선)
//   [1, N, 4 + num_classes]  (전치된 형식)
//   각 anchor = cx, cy, w, h (모델 입력 픽셀 좌표), class별 점수 (0~1, sigmoid 적용 후)
//
// 출력 Detection:
//   x, y = bbox 좌상단, width, height  (원본 LWIR 영상 픽셀 좌표, TLWH)
//   → ByteTrackTracker::track()에 그대로 넣을 수 있다.
//
// 처리 순서: 점수 필터 → class별 NMS → letterbox 역변환 → 원본 영상 경계로 자르기
class Yolov8Postprocessor
{
public:
    explicit Yolov8Postprocessor(
        const ModelPostprocessConfig& config,
        int max_detections = 300
    );

    std::vector<Detection> process(
        const OutputTensorView& output,
        const LwirPreprocessContext& context
    ) const;

private:
    ModelPostprocessConfig config_;
    int max_detections_;
};
