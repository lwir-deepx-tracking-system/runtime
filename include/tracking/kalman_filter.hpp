#pragma once

#include <array>

// ByteTrack용 Kalman Filter (외부 라이브러리 없이 구현). 구현은 src/tracking/kalman_filter.cpp
// 등속 운동 모델이며 두 가지 상태 형식을 지원한다 (노이즈 계수 1/20, 1/160은 같다).
//   XYAH (기본, 공식 ByteTrack): 상태 [cx, cy, a, h, vcx, vcy, va, vh], 측정 [cx, cy, a, h]  (a = width / height)
//        노이즈는 높이 h에 비례하고, 가로세로비 a는 거의 변하지 않는다고 가정한다 (작은 고정 노이즈).
//   XYWH (BoT-SORT): 상태 [cx, cy, w, h, vcx, vcy, vw, vh], 측정 [cx, cy, w, h]
//        x·폭 관련 노이즈는 폭 w에, y·높이 관련 노이즈는 높이 h에 비례한다. 폭 변화를 높이처럼 빠르게 따라간다.

using KalmanVec8 = std::array<double, 8>;
using KalmanMat8 = std::array<std::array<double, 8>, 8>;
using KalmanMeasurement = std::array<double, 4>;

// Track 하나의 Kalman 상태. Track마다 따로 가지고, KalmanFilter는 상태 없이 계산만 한다.
struct KalmanState
{
    KalmanVec8 mean{};
    KalmanMat8 covariance{};
    bool xywh = false;  // 상태 형식 (initiate가 정한다). 박스 변환·CMC가 이 값을 보고 mean[2]를 해석한다
};

enum class KalmanFormat
{
    XYAH,
    XYWH,
};

class KalmanFilter
{
public:
    explicit KalmanFilter(KalmanFormat format = KalmanFormat::XYAH) : format_(format) {}

    KalmanFormat format() const { return format_; }

    // 첫 검출로 새 Track의 상태를 만든다.
    KalmanState initiate(const KalmanMeasurement& z) const;

    // 한 프레임 뒤의 위치를 예측한다. x' = F x, P' = F P F^T + Q
    void predict(KalmanState& s) const;

    // 매칭된 검출로 상태를 보정한다. 혁신 공분산이 특이행렬이면 상태를 바꾸지 않는다.
    // noise_scale: 측정 노이즈 표준편차에 곱하는 값 (1: 공식 ByteTrack, 작을수록 검출을 더 믿음).
    //              NSA Kalman에서 검출 점수로 계산해 넘긴다.
    void update(KalmanState& s, const KalmanMeasurement& z, double noise_scale = 1.0) const;

private:
    KalmanFormat format_;
};
