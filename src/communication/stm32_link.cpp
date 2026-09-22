#include "communication/stm32_link.hpp"

// 실제 STM32 전송은 아직 구현하지 않은 구조용 진입점이다.
void STM32Link::send(const TargetSelection& selection)
{
    (void)selection;
    // TODO: STM32 통신 및 목표 상실 시 제어 정책
}
