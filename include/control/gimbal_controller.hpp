#pragma once

#include "common/frame.hpp"
#include "common/track.hpp"

// Orange Pi에서 짐벌 하드웨어를 직접 제어하는 경계.
// 실제 GPIO/PWM/I2C 드라이버는 하드웨어 방식이 정해지면 추가한다.
class GimbalController
{
public:
    // target이 nullptr이면 선택 해제 또는 현재 frame에서 target 유실 상태이다.
    void apply(const Track* target, const FrameContext& frame);
};
