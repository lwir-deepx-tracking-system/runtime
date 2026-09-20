#pragma once

#include "common/frame_metadata.hpp"

// Inference 결과. 실제 Tensor 정보는 Inference 구현 시 추가한다.
struct ModelOutput
{
    FrameMetadata metadata;
};
