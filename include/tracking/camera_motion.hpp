#pragma once

#include <cmath>

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
    static CameraMotion translation(double dx, double dy)
    {
        CameraMotion m;
        m.tx = dx;
        m.ty = dy;
        return m;
    }

    bool is_identity() const
    {
        return a == 1.0 && b == 0.0 && c == 0.0 && d == 1.0 && tx == 0.0 && ty == 0.0;
    }

    // 변환의 확대 비율 (회전·확대 행렬의 행렬식 제곱근). 줌이 없으면 1
    double scale() const
    {
        const double det = a * d - b * c;
        return det > 0.0 ? std::sqrt(det) : 1.0;
    }
};

// Kalman 상태 [cx, cy, aspect, h, vcx, vcy, vaspect, vh]를 카메라 움직임만큼 옮긴다.
//   중심 (cx, cy)   : 회전·확대 후 평행이동
//   속도 (vcx, vcy) : 회전·확대만 (평행이동은 속도에 영향 없음)
//   높이 h, vh      : 확대 비율만큼
//   aspect          : 균일 확대에서는 변하지 않음
// 공분산도 같은 변환 T로 P' = T P T^T 를 적용해 불확실성의 방향과 크기를 맞춘다.
inline void apply_camera_motion(KalmanState& s, const CameraMotion& m)
{
    if (m.is_identity())
        return;

    const double k = m.scale();
    auto& x = s.mean;

    const double cx = x[0], cy = x[1];
    x[0] = m.a * cx + m.b * cy + m.tx;
    x[1] = m.c * cx + m.d * cy + m.ty;
    x[3] *= k;

    const double vx = x[4], vy = x[5];
    x[4] = m.a * vx + m.b * vy;
    x[5] = m.c * vx + m.d * vy;
    x[7] *= k;

    // T: (0,1)과 (4,5)는 2x2 회전·확대 블록, 3과 7은 k, 2와 6은 1
    KalmanMat8 t{};
    for (int base : {0, 4})
    {
        t[base][base] = m.a;
        t[base][base + 1] = m.b;
        t[base + 1][base] = m.c;
        t[base + 1][base + 1] = m.d;
    }
    t[2][2] = 1.0;
    t[6][6] = 1.0;
    t[3][3] = k;
    t[7][7] = k;

    const KalmanMat8& p = s.covariance;
    KalmanMat8 tp{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            for (int l = 0; l < 8; ++l)
                tp[i][j] += t[i][l] * p[l][j];
    KalmanMat8 out{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            for (int l = 0; l < 8; ++l)
                out[i][j] += tp[i][l] * t[j][l];
    s.covariance = out;
}
