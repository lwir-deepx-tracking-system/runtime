#include "tracking/camera_motion.hpp"

#include <cmath>

CameraMotion CameraMotion::translation(double dx, double dy)
{
    CameraMotion m;
    m.tx = dx;
    m.ty = dy;
    return m;
}

bool CameraMotion::is_identity() const
{
    return a == 1.0 && b == 0.0 && c == 0.0 && d == 1.0 && tx == 0.0 && ty == 0.0;
}

double CameraMotion::scale() const
{
    const double det = a * d - b * c;
    return det > 0.0 ? std::sqrt(det) : 1.0;
}

void apply_camera_motion(KalmanState& s, const CameraMotion& m)
{
    if (m.is_identity())
        return;

    const double k = m.scale();
    auto& x = s.mean;

    const double cx = x[0], cy = x[1];
    x[0] = m.a * cx + m.b * cy + m.tx;
    x[1] = m.c * cx + m.d * cy + m.ty;
    x[3] *= k;
    if (s.xywh)
        x[2] *= k;

    const double vx = x[4], vy = x[5];
    x[4] = m.a * vx + m.b * vy;
    x[5] = m.c * vx + m.d * vy;
    x[7] *= k;
    if (s.xywh)
        x[6] *= k;

    // T: (0,1)과 (4,5)는 2x2 회전·확대 블록, 3과 7은 k, 2와 6은 1
    KalmanMat8 t{};
    for (int base : {0, 4})
    {
        t[base][base] = m.a;
        t[base][base + 1] = m.b;
        t[base + 1][base] = m.c;
        t[base + 1][base + 1] = m.d;
    }
    t[2][2] = s.xywh ? k : 1.0;
    t[6][6] = s.xywh ? k : 1.0;
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
