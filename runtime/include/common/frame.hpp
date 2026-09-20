#pragma once
#include <opencv2/core.hpp>
#include "common/frame_metadata.hpp"

// Camera에서 전달하는 영상과 프레임 식별 정보.

struct Frame
{
    FrameMetadata metadata;
    cv::Mat image;   // 640x480, 16-bit single channel raw
};
