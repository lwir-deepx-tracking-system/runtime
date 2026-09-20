#pragma once

#include "target/target_selector.hpp"

// STM32 전송 경계. 실제 프로토콜 구현은 추후 추가한다.
class STM32Link
{
public:
    // 실제 전송 형식과 통신 방식은 추후 정한다.
    void send(const TargetSelection& selection);
};
