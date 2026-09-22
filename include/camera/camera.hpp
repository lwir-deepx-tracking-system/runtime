#pragma once

#include "common/frame.hpp"

class Camera
{
public:
    // 카메라 사용 시작
    bool open();
    // 프레임 읽기
    bool read(Frame& frame);
    // 카메라 사용 종료
    void close();
};