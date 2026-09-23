#include "control/gimbal_controller.hpp"

// 직접 제어 방식이 정해지기 전까지 구조만 유지한다.
void GimbalController::apply(const TargetSelection& selection)
{
    (void)selection;
    // TODO: Orange Pi 짐벌 제어 및 목표 상실 시 정지 정책
}
