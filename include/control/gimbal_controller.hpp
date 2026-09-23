#pragma once

#include "target/target_selector.hpp"

// Orange Pi에서 짐벌 하드웨어를 직접 제어하는 경계.
// 실제 GPIO/PWM/I2C 드라이버는 하드웨어 방식이 정해지면 추가한다.
class GimbalController
{
public:
    void apply(const TargetSelection& selection);
};
