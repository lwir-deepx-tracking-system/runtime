#pragma once

#include "common/frame_metadata.hpp"

// Preprocess 결과로 생성되는 모델 입력 데이터.
// 실제 Tensor 정보는 Inference 구현 시 추가한다.
struct ModelInput
{
    FrameMetadata metadata;
};
