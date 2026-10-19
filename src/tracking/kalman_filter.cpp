#include "tracking/kalman_filter.hpp"

#include <cmath>
#include <utility>

namespace
{

// 공식 ByteTrack 노이즈 계수 (높이 h에 비례)
constexpr double kStdPos = 1.0 / 20.0;
constexpr double kStdVel = 1.0 / 160.0;

// 4x4 역행렬 (Gauss-Jordan, 부분 피벗). 특이행렬이면 false
bool invert4(const double in[4][4], double out[4][4])
{
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            a[i][j] = in[i][j];
            a[i][j + 4] = (i == j) ? 1.0 : 0.0;
        }

    for (int col = 0; col < 4; ++col)
    {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col]))
                pivot = row;
        if (std::fabs(a[pivot][col]) < 1e-12)
            return false;
        if (pivot != col)
            for (int j = 0; j < 8; ++j)
                std::swap(a[pivot][j], a[col][j]);

        const double d = a[col][col];
        for (int j = 0; j < 8; ++j)
            a[col][j] /= d;

        for (int row = 0; row < 4; ++row)
        {
            if (row == col)
                continue;
            const double f = a[row][col];
            for (int j = 0; j < 8; ++j)
                a[row][j] -= f * a[col][j];
        }
    }

    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out[i][j] = a[i][j + 4];
    return true;
}

}  // namespace


KalmanState KalmanFilter::initiate(const KalmanMeasurement& z) const
{
    KalmanState s;
    s.xywh = (format_ == KalmanFormat::XYWH);
    for (int i = 0; i < 4; ++i)
        s.mean[i] = z[i];

    const double h = z[3];
    const double w = s.xywh ? z[2] : h;  // XYAH는 모든 노이즈가 h 기준
    const double std_dev[8] = {
        2 * kStdPos * w, 2 * kStdPos * h, s.xywh ? 2 * kStdPos * w : 1e-2, 2 * kStdPos * h,
        10 * kStdVel * w, 10 * kStdVel * h, s.xywh ? 10 * kStdVel * w : 1e-5, 10 * kStdVel * h};
    for (int i = 0; i < 8; ++i)
        s.covariance[i][i] = std_dev[i] * std_dev[i];
    return s;
}


void KalmanFilter::predict(KalmanState& s) const
{
    const double h = s.mean[3];
    const double w = s.xywh ? s.mean[2] : h;
    const double q[8] = {
        kStdPos * w, kStdPos * h, s.xywh ? kStdPos * w : 1e-2, kStdPos * h,
        kStdVel * w, kStdVel * h, s.xywh ? kStdVel * w : 1e-5, kStdVel * h};

    for (int i = 0; i < 4; ++i)
        s.mean[i] += s.mean[i + 4];

    // F = [[I, I], [0, I]]
    const KalmanMat8& p = s.covariance;
    KalmanMat8 fp{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            fp[i][j] = p[i][j] + (i < 4 ? p[i + 4][j] : 0.0);

    KalmanMat8 fpf{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            fpf[i][j] = fp[i][j] + (j < 4 ? fp[i][j + 4] : 0.0);

    for (int i = 0; i < 8; ++i)
        fpf[i][i] += q[i] * q[i];

    s.covariance = fpf;
}


void KalmanFilter::update(KalmanState& s, const KalmanMeasurement& z, double noise_scale) const
{
    const double h = s.mean[3];
    const double w = s.xywh ? s.mean[2] : h;
    const double r[4] = {
        noise_scale * kStdPos * w, noise_scale * kStdPos * h,
        noise_scale * (s.xywh ? kStdPos * w : 1e-1), noise_scale * kStdPos * h};
    const KalmanMat8& p = s.covariance;

    // S = H P H^T + R  (H = [I 0])
    double S[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            S[i][j] = p[i][j] + (i == j ? r[i] * r[i] : 0.0);

    double S_inv[4][4];
    if (!invert4(S, S_inv))
        return;

    // K = P H^T S^-1  (8x4)
    double K[8][4];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j)
        {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k)
                sum += p[i][k] * S_inv[k][j];
            K[i][j] = sum;
        }

    double innovation[4];
    for (int i = 0; i < 4; ++i)
        innovation[i] = z[i] - s.mean[i];

    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j)
            s.mean[i] += K[i][j] * innovation[j];

    // P = P - K S K^T = P - K (H P)
    KalmanMat8 new_p{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
        {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k)
                sum += K[i][k] * p[k][j];
            new_p[i][j] = p[i][j] - sum;
        }

    // 수치 오차로 대칭이 깨지지 않게 맞춘다.
    for (int i = 0; i < 8; ++i)
        for (int j = i + 1; j < 8; ++j)
        {
            const double avg = 0.5 * (new_p[i][j] + new_p[j][i]);
            new_p[i][j] = avg;
            new_p[j][i] = avg;
        }

    s.covariance = new_p;
}
