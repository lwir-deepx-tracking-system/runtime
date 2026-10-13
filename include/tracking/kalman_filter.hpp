#pragma once

#include <array>

// ByteTrack용 Kalman Filter (외부 라이브러리 없이 구현). 구현은 src/tracking/kalman_filter.cpp
// 상태: [cx, cy, a, h, vcx, vcy, va, vh]  (a = width / height)
// 측정: [cx, cy, a, h]
// 등속 운동 모델이며, 노이즈 계수는 공식 ByteTrack 구현과 같다.

using KalmanVec8 = std::array<double, 8>;
using KalmanMat8 = std::array<std::array<double, 8>, 8>;
using KalmanMeasurement = std::array<double, 4>;

// Track 하나의 Kalman 상태. Track마다 따로 가지고, KalmanFilter는 상태 없이 계산만 한다.
struct KalmanState
{
    KalmanVec8 mean{};
    KalmanMat8 covariance{};
};

class KalmanFilter
{
public:
    // 첫 검출로 새 Track의 상태를 만든다.
    KalmanState initiate(const KalmanMeasurement& z) const;

    // 한 프레임 뒤의 위치를 예측한다. x' = F x, P' = F P F^T + Q
    void predict(KalmanState& s) const;

    // 매칭된 검출로 상태를 보정한다. 혁신 공분산이 특이행렬이면 상태를 바꾸지 않는다.
    void update(KalmanState& s, const KalmanMeasurement& z) const;
};
