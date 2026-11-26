#pragma once

#include "tracking/kalman_filter.hpp"

// 카메라 움직임 보정(CMC)에 쓰는 프레임 간 변환.
//
// 직전 프레임 영상 좌표 (x, y)가 현재 프레임에서 어디로 옮겨 보이는지를 나타낸다.
//   x' = a*x + b*y + tx
//   y' = c*x + d*y + ty
// 짐벌이 오른쪽으로 10px만큼 돌면 화면 속 물체는 왼쪽으로 밀리므로 translation(-10, 0)이다.
//
// 이 파일은 계산만 담당한다. 움직임을 어떻게 구할지(영상 특징점, 짐벌 각도 등)는
// 별도 추정기가 정하고, Tracker는 ByteTrackTracker::set_camera_motion()으로 결과만 받는다.
struct CameraMotion
{
    double a = 1.0, b = 0.0, tx = 0.0;
    double c = 0.0, d = 1.0, ty = 0.0;

    // 평행이동만 있는 움직임 (짐벌 팬/틸트의 근사)
    static CameraMotion translation(double dx, double dy);

    bool is_identity() const;

    // 변환의 확대 비율 (회전·확대 행렬의 행렬식 제곱근). 줌이 없으면 1
    double scale() const;
};

// Kalman 상태 [cx, cy, aspect 또는 w, h, vcx, vcy, vaspect 또는 vw, vh]를 카메라 움직임만큼 옮긴다.
//   중심 (cx, cy)   : 회전·확대 후 평행이동
//   속도 (vcx, vcy) : 회전·확대만 (평행이동은 속도에 영향 없음)
//   높이 h, vh      : 확대 비율만큼
//   aspect (XYAH)   : 균일 확대에서는 변하지 않음 / 폭 w, vw (XYWH): 확대 비율만큼
// 공분산도 같은 변환 T로 P' = T P T^T 를 적용해 불확실성의 방향과 크기를 맞춘다.
void apply_camera_motion(KalmanState& s, const CameraMotion& m);
