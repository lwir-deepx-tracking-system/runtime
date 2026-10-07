#pragma once

#include "common/frame.hpp"

namespace i3
{
class TE_A;
}

class Camera
{
private:
    i3::TE_A* te_ = nullptr;
    int width_ = 0;
    int height_ = 0;

public:
    Camera() = default;
    ~Camera();
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    // 카메라 사용 시작
    bool open();
    // 프레임 읽기
    bool capture(FrameContext& frame);
    // 카메라 사용 종료
    void close();
};
