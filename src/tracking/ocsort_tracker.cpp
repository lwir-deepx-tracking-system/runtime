#include "tracking/ocsort_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

#include "common/logger.hpp"

// 공식 구현 대응표 (github.com/noahcao/OC_SORT, trackers/ocsort_tracker)
//   ocsort.py        KalmanBoxTracker.predict/update → OCSortTracker::predict/update
//                    OCSort.update                   → OCSortTracker::track
//                    k_previous_obs, speed_direction → previous_observation, speed_direction
//   association.py   associate, iou_batch            → associate, iou
//   kalmanfilter.py  KalmanFilterNew.predict/update  → kf_predict, kf_update
//                    freeze/unfreeze (ORU)           → kf_update 안의 저장·재계산

using ocsort::Box5;
using ocsort::Mat7;
using ocsort::OCTrack;
using ocsort::Observation;
using ocsort::Vec7;

namespace
{

using Z4 = std::array<double, 4>;  // 측정값 [cx, cy, s, r]

constexpr double kLowScore = 0.1;  // use_byte 2차 매칭 검출의 하한 (공식 구현 고정값)

// ---- SORT Kalman 상수 (공식: filterpy 기본 단위행렬에 배율 적용) ----
//   R = diag(1, 1, 10, 10)
//   P0 = diag(10, 10, 10, 10, 10000, 10000, 10000)
//   Q = diag(1, 1, 1, 1, 0.01, 0.01, 0.0001)
constexpr double kR[4] = {1.0, 1.0, 10.0, 10.0};
constexpr double kP0[7] = {10.0, 10.0, 10.0, 10.0, 1e4, 1e4, 1e4};
constexpr double kQ[7] = {1.0, 1.0, 1.0, 1.0, 1e-2, 1e-2, 1e-4};

// [x1, y1, x2, y2] → [cx, cy, s, r]
Z4 to_z(const Box5& b)
{
    const double w = b[2] - b[0];
    const double h = b[3] - b[1];
    return {b[0] + w / 2.0, b[1] + h / 2.0, w * h, w / (h + 1e-6)};
}

// Kalman 상태 → [x1, y1, x2, y2]. s * r가 음수면 NaN (공식 구현과 같이 그 Track을 지운다)
std::array<double, 4> state_box(const Vec7& x)
{
    const double w = std::sqrt(x[2] * x[3]);
    const double h = x[2] / w;
    return {x[0] - w / 2.0, x[1] - h / 2.0, x[0] + w / 2.0, x[1] + h / 2.0};
}

// x' = F x, P' = F P F^T + Q. F는 (cx, cy, s)에 각 속도를 더한다.
void kf_predict(Vec7& x, Mat7& P)
{
    for (int i = 0; i < 3; ++i)
        x[i] += x[i + 4];

    Mat7 fp{};
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j)
            fp[i][j] = P[i][j] + (i < 3 ? P[i + 4][j] : 0.0);
    Mat7 fpf{};
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j)
            fpf[i][j] = fp[i][j] + (j < 3 ? fp[i][j + 4] : 0.0);
    for (int i = 0; i < 7; ++i)
        fpf[i][i] += kQ[i];
    P = fpf;
}

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

// 표준 Kalman update (H = [I 0]). 공분산은 공식 구현처럼 Joseph 형식으로 갱신한다:
//   P = (I - KH) P (I - KH)^T + K R K^T
void kf_correct(Vec7& x, Mat7& P, const Z4& z)
{
    double S[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            S[i][j] = P[i][j] + (i == j ? kR[i] : 0.0);
    double SI[4][4];
    if (!invert4(S, SI))
        return;

    double K[7][4];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 4; ++j)
        {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k)
                sum += P[i][k] * SI[k][j];
            K[i][j] = sum;
        }

    double y[4];
    for (int i = 0; i < 4; ++i)
        y[i] = z[i] - x[i];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 4; ++j)
            x[i] += K[i][j] * y[j];

    // A = I - K H (7x7, 오른쪽 3열은 단위행렬 그대로)
    Mat7 A{};
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j)
            A[i][j] = (i == j ? 1.0 : 0.0) - (j < 4 ? K[i][j] : 0.0);
    Mat7 AP{};
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j)
            for (int k = 0; k < 7; ++k)
                AP[i][j] += A[i][k] * P[k][j];
    Mat7 out{};
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j)
        {
            double sum = 0.0;
            for (int k = 0; k < 7; ++k)
                sum += AP[i][k] * A[j][k];
            for (int k = 0; k < 4; ++k)
                sum += K[i][k] * kR[k] * K[j][k];
            out[i][j] = sum;
        }
    P = out;
}

// 공식 iou_batch와 같은 계산 (x1, y1, x2, y2). 넓이 합이 0이면 0
double iou(const double* a, const double* b)
{
    const double xx1 = std::max(a[0], b[0]), yy1 = std::max(a[1], b[1]);
    const double xx2 = std::min(a[2], b[2]), yy2 = std::min(a[3], b[3]);
    const double wh = std::max(0.0, xx2 - xx1) * std::max(0.0, yy2 - yy1);
    const double uni = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - wh;
    return uni > 0.0 ? wh / uni : 0.0;
}

// 공식 speed_direction: bbox1 → bbox2 중심 방향의 단위 벡터 (dy, dx). 길이에 1e-6을 더한다
std::pair<double, double> speed_direction(const Box5& b1, const Box5& b2)
{
    const double cx1 = (b1[0] + b1[2]) / 2.0, cy1 = (b1[1] + b1[3]) / 2.0;
    const double cx2 = (b2[0] + b2[2]) / 2.0, cy2 = (b2[1] + b2[3]) / 2.0;
    const double norm = std::sqrt((cy2 - cy1) * (cy2 - cy1) + (cx2 - cx1) * (cx2 - cx1)) + 1e-6;
    return {(cy2 - cy1) / norm, (cx2 - cx1) / norm};
}

// 공식 k_previous_obs: 나이 cur_age 기준 k, k-1, ..., 1 프레임 전 관측 중 처음 찾은 것. 없으면 가장 최근 관측.
// 관측이 하나도 없으면 nullptr (공식의 [-1, -1, -1, -1, -1] 자리표시)
const Box5* previous_observation(const OCTrack& t, int cur_age, int k)
{
    if (t.observations.empty())
        return nullptr;
    for (int dt = k; dt >= 1; --dt)
        for (const auto& o : t.observations)
            if (o.age == cur_age - dt)
                return &o.box;
    return &t.observations.back().box;
}

// 최소 비용 할당 (헝가리안). 행렬 전체로 짝을 정하고 min(행, 열)개의 (행, 열) 쌍을 행 순서로 반환한다
// (공식 linear_assignment = scipy linear_sum_assignment / lap.lapjv와 같은 해, 행 순서 정렬)
std::vector<std::pair<int, int>> assign_min_cost(const std::vector<std::vector<double>>& cost)
{
    std::vector<std::pair<int, int>> out;
    const int rows = static_cast<int>(cost.size());
    if (rows == 0)
        return out;
    const int cols = static_cast<int>(cost[0].size());
    if (cols == 0)
        return out;
    const bool transpose = rows > cols;
    const int n = transpose ? cols : rows;
    const int m = transpose ? rows : cols;
    auto a = [&](int i, int j) { return transpose ? cost[j][i] : cost[i][j]; };

    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> u(n + 1, 0.0), v(m + 1, 0.0);
    std::vector<int> p(m + 1, 0), way(m + 1, 0);
    for (int i = 1; i <= n; ++i)
    {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(m + 1, inf);
        std::vector<char> used(m + 1, 0);
        do
        {
            used[j0] = 1;
            const int i0 = p[j0];
            double delta = inf;
            int j1 = 0;
            for (int j = 1; j <= m; ++j)
            {
                if (used[j])
                    continue;
                const double cur = a(i0 - 1, j - 1) - u[i0] - v[j];
                if (cur < minv[j])
                {
                    minv[j] = cur;
                    way[j] = j0;
                }
                if (minv[j] < delta)
                {
                    delta = minv[j];
                    j1 = j;
                }
            }
            for (int j = 0; j <= m; ++j)
            {
                if (used[j])
                {
                    u[p[j]] += delta;
                    v[j] -= delta;
                }
                else
                {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);
        do
        {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }
    for (int j = 1; j <= m; ++j)
        if (p[j] != 0)
            out.emplace_back(transpose ? j - 1 : p[j] - 1, transpose ? p[j] - 1 : j - 1);
    std::sort(out.begin(), out.end());
    return out;
}

// np.setdiff1d: a에서 remove를 뺀 뒤 정렬·중복 제거
std::vector<int> setdiff(const std::vector<int>& a, const std::vector<int>& remove)
{
    std::vector<int> out;
    for (int v : a)
        if (std::find(remove.begin(), remove.end(), v) == remove.end())
            out.push_back(v);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

struct Association
{
    std::vector<std::pair<int, int>> matches;  // (검출, Track)
    std::vector<int> unmatched_dets;
    std::vector<int> unmatched_trks;
};

// 공식 associate (1차 매칭): IoU + OCM 방향 일치도로 짝을 정하고 IoU iou_threshold 미만은 버린다.
//   방향 일치도 = (π/2 - |두 방향 사이 각|) / π, 관측이 없는 Track은 0. 검출 점수와 inertia를 곱한다.
//   IoU 기준을 넘는 후보가 행·열마다 하나뿐이면 헝가리안 없이 그대로 짝짓는다.
Association associate(const std::vector<Box5>& dets, const std::vector<std::array<double, 4>>& trks,
                      double iou_threshold, const std::vector<std::pair<double, double>>& velocities,
                      const std::vector<const Box5*>& previous_obs, double vdc_weight)
{
    Association r;
    const int nd = static_cast<int>(dets.size()), nt = static_cast<int>(trks.size());
    if (nt == 0)
    {
        for (int d = 0; d < nd; ++d)
            r.unmatched_dets.push_back(d);
        return r;
    }

    constexpr double kPi = 3.14159265358979323846;
    std::vector<std::vector<double>> iou_m(nd, std::vector<double>(nt, 0.0)), angle(nd, std::vector<double>(nt, 0.0));
    for (int d = 0; d < nd; ++d)
        for (int t = 0; t < nt; ++t)
        {
            iou_m[d][t] = iou(dets[d].data(), trks[t].data());
            const Box5* prev = previous_obs[t];
            if (!prev)
                continue;  // valid_mask = 0
            const double pcx = ((*prev)[0] + (*prev)[2]) / 2.0, pcy = ((*prev)[1] + (*prev)[3]) / 2.0;
            const double dcx = (dets[d][0] + dets[d][2]) / 2.0, dcy = (dets[d][1] + dets[d][3]) / 2.0;
            const double norm = std::sqrt((dcx - pcx) * (dcx - pcx) + (dcy - pcy) * (dcy - pcy)) + 1e-6;
            const double X = (dcx - pcx) / norm, Y = (dcy - pcy) / norm;
            const double c = std::clamp(velocities[t].second * X + velocities[t].first * Y, -1.0, 1.0);
            angle[d][t] = (kPi / 2.0 - std::fabs(std::acos(c))) / kPi * vdc_weight * dets[d][4];
        }

    std::vector<std::pair<int, int>> matched;
    if (nd > 0)
    {
        std::vector<int> row_sum(nd, 0), col_sum(nt, 0);
        for (int d = 0; d < nd; ++d)
            for (int t = 0; t < nt; ++t)
                if (iou_m[d][t] > iou_threshold)
                {
                    ++row_sum[d];
                    ++col_sum[t];
                }
        const int max_row = *std::max_element(row_sum.begin(), row_sum.end());
        const int max_col = *std::max_element(col_sum.begin(), col_sum.end());
        if (max_row == 1 && max_col == 1)
        {
            for (int d = 0; d < nd; ++d)
                for (int t = 0; t < nt; ++t)
                    if (iou_m[d][t] > iou_threshold)
                        matched.emplace_back(d, t);
        }
        else
        {
            std::vector<std::vector<double>> cost(nd, std::vector<double>(nt));
            for (int d = 0; d < nd; ++d)
                for (int t = 0; t < nt; ++t)
                    cost[d][t] = -(iou_m[d][t] + angle[d][t]);
            matched = assign_min_cost(cost);
        }
    }

    // 공식 _filter_matches: 짝이 없는 것(정렬) 뒤에 IoU가 낮아 버린 짝을 그 순서대로 붙인다
    std::vector<int> md, mt;
    for (const auto& [d, t] : matched)
    {
        md.push_back(d);
        mt.push_back(t);
    }
    std::vector<int> all_d(nd), all_t(nt);
    for (int i = 0; i < nd; ++i)
        all_d[i] = i;
    for (int i = 0; i < nt; ++i)
        all_t[i] = i;
    r.unmatched_dets = setdiff(all_d, md);
    r.unmatched_trks = setdiff(all_t, mt);
    for (const auto& [d, t] : matched)
    {
        if (iou_m[d][t] < iou_threshold)
        {
            r.unmatched_dets.push_back(d);
            r.unmatched_trks.push_back(t);
        }
        else
        {
            r.matches.emplace_back(d, t);
        }
    }
    return r;
}

// x1, y1, x2, y2 박스에 카메라 움직임 적용 (중심은 회전·확대 후 평행이동, 크기는 확대 비율만큼)
void warp_box(double* b, const CameraMotion& m)
{
    const double k = m.scale();
    const double cx = (b[0] + b[2]) / 2.0, cy = (b[1] + b[3]) / 2.0;
    const double w = (b[2] - b[0]) * k, h = (b[3] - b[1]) * k;
    const double nx = m.a * cx + m.b * cy + m.tx, ny = m.c * cx + m.d * cy + m.ty;
    b[0] = nx - w / 2.0;
    b[1] = ny - h / 2.0;
    b[2] = nx + w / 2.0;
    b[3] = ny + h / 2.0;
}

// SORT 상태 [cx, cy, s, r, vcx, vcy, vs]에 카메라 움직임 적용. 넓이 s와 vs는 확대 비율의 제곱, r은 그대로.
// 공분산은 같은 선형 변환 T로 P' = T P T^T
void warp_state(Vec7& x, Mat7& P, const CameraMotion& m)
{
    const double k2 = m.scale() * m.scale();
    const double cx = x[0], cy = x[1], vx = x[4], vy = x[5];
    x[0] = m.a * cx + m.b * cy + m.tx;
    x[1] = m.c * cx + m.d * cy + m.ty;
    x[4] = m.a * vx + m.b * vy;
    x[5] = m.c * vx + m.d * vy;
    x[2] *= k2;
    x[6] *= k2;

    Mat7 T{};
    for (int base : {0, 4})
    {
        T[base][base] = m.a;
        T[base][base + 1] = m.b;
        T[base + 1][base] = m.c;
        T[base + 1][base + 1] = m.d;
    }
    T[2][2] = k2;
    T[3][3] = 1.0;
    T[6][6] = k2;
    Mat7 TP{};
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j)
            for (int l = 0; l < 7; ++l)
                TP[i][j] += T[i][l] * P[l][j];
    Mat7 out{};
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j)
            for (int l = 0; l < 7; ++l)
                out[i][j] += TP[i][l] * T[j][l];
    P = out;
}

}  // namespace


OCSortTracker::OCSortTracker(const OCSortConfig& config) : config_(config)
{
    std::ostringstream values;
    values << "det_threshold=" << config_.det_threshold << ", iou_threshold=" << config_.iou_threshold
           << ", max_age=" << config_.max_age << ", min_hits=" << config_.min_hits
           << ", delta_t=" << config_.delta_t << ", inertia=" << config_.inertia
           << ", use_byte=" << (config_.use_byte ? "true" : "false");
    Logger::info("[Tracking] OCSortTracker 생성 완료");
    Logger::debug("[Tracking] OC-SORT 설정 값: " + values.str());
}


void OCSortTracker::reset()
{
    trackers_.clear();
    frame_count_ = 0;
    next_id_ = 0;
    camera_motion_ = CameraMotion{};
    Logger::info("[Tracking] OCSortTracker 상태 초기화 완료");
}


// 공식 KalmanBoxTracker.__init__: 검출 박스로 상태를 만든다. 이 검출은 관측 기록에 넣지 않는다 (공식과 같음)
OCTrack OCSortTracker::create_track(const Box5& det, int class_id)
{
    OCTrack t;
    t.id = next_id_++;
    const Z4 z = to_z(det);
    for (int i = 0; i < 4; ++i)
        t.x[i] = z[i];
    for (int i = 0; i < 7; ++i)
        t.P[i][i] = kP0[i];
    t.class_id = class_id;
    t.score = det[4];
    return t;
}


// 공식 KalmanBoxTracker.predict
void OCSortTracker::predict(OCTrack& t) const
{
    if (t.x[6] + t.x[2] <= 0.0)
        t.x[6] = 0.0;
    kf_predict(t.x, t.P);
    ++t.age;
    if (t.time_since_update > 0)
        t.hit_streak = 0;
    ++t.time_since_update;
}


// 공식 KalmanBoxTracker.update + KalmanFilterNew.update (ORU 포함)
void OCSortTracker::update(OCTrack& t, const Box5* det, int class_id) const
{
    if (!det)
    {
        // 관측 없음: 직전까지 관측이 있었으면 지금(놓친 첫 프레임의 예측) 상태를 저장한다 (freeze)
        if (t.observed)
        {
            t.saved_x = t.x;
            t.saved_P = t.P;
            t.frozen = true;
        }
        t.observed = false;
        ++t.misses_since_z;
        return;
    }

    // OCM용 이동 방향: delta_t, ..., 1 프레임 전 관측(없으면 마지막 관측) → 이번 검출
    if (t.has_last_observation)
    {
        const Box5* prev = nullptr;
        for (int dt = config_.delta_t; dt >= 1 && !prev; --dt)
            for (const auto& o : t.observations)
                if (o.age == t.age - dt)
                {
                    prev = &o.box;
                    break;
                }
        if (!prev)
            prev = &t.last_observation;
        const auto [vy, vx] = speed_direction(*prev, *det);
        t.velocity_y = vy;
        t.velocity_x = vx;
        t.has_velocity = true;
    }
    t.last_observation = *det;
    t.has_last_observation = true;
    t.observations.push_back({t.age, *det});
    // 이후 조회는 (지금 age + 1) - delta_t 이상만 보므로 그보다 오래된 관측은 버린다 (마지막 관측은 항상 남김)
    while (t.observations.size() > 1 && t.observations.front().age < t.age - config_.delta_t)
        t.observations.pop_front();

    t.time_since_update = 0;
    t.hits += 1;
    t.hit_streak += 1;
    t.class_id = class_id;
    t.score = (*det)[4];

    const Z4 z = to_z(*det);
    // ORU (공식 unfreeze): 저장한 상태로 되돌리고, 마지막 측정 → 이번 측정을 직선 보간한 가상 측정으로
    // 놓친 구간을 update/predict 한다. 폭·높이 공간에서 보간한다.
    if (!t.observed && t.frozen)
    {
        t.x = t.saved_x;
        t.P = t.saved_P;
        t.frozen = false;
        const int gap = t.misses_since_z + 1;
        const Z4& z1 = t.last_z;
        const double w1 = std::sqrt(z1[2] * z1[3]), h1 = std::sqrt(z1[2] / z1[3]);
        const double w2 = std::sqrt(z[2] * z[3]), h2 = std::sqrt(z[2] / z[3]);
        if (std::isfinite(w1) && std::isfinite(h1) && std::isfinite(w2) && std::isfinite(h2) && h1 > 0.0 && h2 > 0.0)
        {
            const double dx = (z[0] - z1[0]) / gap, dy = (z[1] - z1[1]) / gap;
            const double dw = (w2 - w1) / gap, dh = (h2 - h1) / gap;
            for (int i = 0; i < gap; ++i)
            {
                const double w = w1 + (i + 1) * dw, h = h1 + (i + 1) * dh;
                kf_correct(t.x, t.P, {z1[0] + (i + 1) * dx, z1[1] + (i + 1) * dy, w * h, w / h});
                if (i != gap - 1)
                    kf_predict(t.x, t.P);
            }
        }
    }
    // 공식 구현은 ORU 마지막 가상 측정(= 이번 측정)으로 update한 뒤 이번 측정으로 한 번 더 update한다. 그대로 따른다.
    t.observed = true;
    kf_correct(t.x, t.P, z);
    t.last_z = z;
    t.misses_since_z = 0;
}


void OCSortTracker::apply_motion(OCTrack& t, const CameraMotion& m) const
{
    warp_state(t.x, t.P, m);
    if (t.frozen)
        warp_state(t.saved_x, t.saved_P, m);
    // ORU 보간 시작점 [cx, cy, s, r]
    const double k2 = m.scale() * m.scale();
    const double cx = t.last_z[0], cy = t.last_z[1];
    t.last_z[0] = m.a * cx + m.b * cy + m.tx;
    t.last_z[1] = m.c * cx + m.d * cy + m.ty;
    t.last_z[2] *= k2;
    if (t.has_last_observation)
        warp_box(t.last_observation.data(), m);
    for (auto& o : t.observations)
        warp_box(o.box.data(), m);
    // 이동 방향(단위 벡터)은 회전만 반영한다
    if (t.has_velocity)
    {
        const double vx = m.a * t.velocity_x + m.b * t.velocity_y;
        const double vy = m.c * t.velocity_x + m.d * t.velocity_y;
        const double n = std::sqrt(vx * vx + vy * vy);
        if (n > 0.0)
        {
            t.velocity_x = vx / n;
            t.velocity_y = vy / n;
        }
    }
}


// 공식 OCSort.update
std::vector<Track> OCSortTracker::track(const std::vector<Detection>& detections)
{
    ++frame_count_;

    // 검출 분류: high(> det_threshold), 2차(0.1 < 점수 < det_threshold, use_byte일 때만)
    std::vector<Box5> dets, dets_second;
    std::vector<int> det_cls, second_cls;
    for (const auto& d : detections)
    {
        if (d.width <= 0.0f || d.height <= 0.0f)
            continue;
        const Box5 b = {d.x, d.y, static_cast<double>(d.x) + d.width, static_cast<double>(d.y) + d.height,
                        d.confidence};
        if (d.confidence > config_.det_threshold)
        {
            dets.push_back(b);
            det_cls.push_back(d.class_id);
        }
        else if (d.confidence > kLowScore && d.confidence < config_.det_threshold)
        {
            dets_second.push_back(b);
            second_cls.push_back(d.class_id);
        }
    }

    // 예측 (+ 카메라 움직임 보정). 예측 박스가 NaN인 Track은 지운다
    const bool move = !camera_motion_.is_identity();
    std::vector<OCTrack> kept;
    std::vector<std::array<double, 4>> trks;
    for (auto& t : trackers_)
    {
        predict(t);
        if (move)
            apply_motion(t, camera_motion_);
        const auto b = state_box(t.x);
        if (std::isnan(b[0]) || std::isnan(b[1]) || std::isnan(b[2]) || std::isnan(b[3]))
            continue;
        trks.push_back(b);
        kept.push_back(std::move(t));
    }
    trackers_ = std::move(kept);
    camera_motion_ = CameraMotion{};

    std::vector<std::pair<double, double>> velocities;
    std::vector<Box5> last_boxes;
    std::vector<const Box5*> k_observations;
    for (const auto& t : trackers_)
    {
        velocities.emplace_back(t.has_velocity ? t.velocity_y : 0.0, t.has_velocity ? t.velocity_x : 0.0);
        last_boxes.push_back(t.last_observation);
        k_observations.push_back(previous_observation(t, t.age, config_.delta_t));
    }

    // 1차 매칭
    Association a = associate(dets, trks, config_.iou_threshold, velocities, k_observations, config_.inertia);
    for (const auto& [d, t] : a.matches)
        update(trackers_[t], &dets[d], det_cls[d]);
    std::vector<int> unmatched_dets = a.unmatched_dets, unmatched_trks = a.unmatched_trks;

    // 2차 매칭 (use_byte): 남은 Track의 예측 박스 ↔ 2차 검출
    if (config_.use_byte && !dets_second.empty() && !unmatched_trks.empty())
    {
        std::vector<std::vector<double>> iou_left(dets_second.size(), std::vector<double>(unmatched_trks.size()));
        double best = -1.0;
        for (size_t d = 0; d < dets_second.size(); ++d)
            for (size_t j = 0; j < unmatched_trks.size(); ++j)
            {
                iou_left[d][j] = iou(dets_second[d].data(), trks[unmatched_trks[j]].data());
                best = std::max(best, iou_left[d][j]);
            }
        if (best > config_.iou_threshold)
        {
            std::vector<std::vector<double>> cost = iou_left;
            for (auto& row : cost)
                for (auto& v : row)
                    v = -v;
            std::vector<int> removed;
            for (const auto& [d, j] : assign_min_cost(cost))
            {
                if (iou_left[d][j] < config_.iou_threshold)
                    continue;
                const int t = unmatched_trks[j];
                update(trackers_[t], &dets_second[d], second_cls[d]);
                removed.push_back(t);
            }
            unmatched_trks = setdiff(unmatched_trks, removed);
        }
    }

    // OCR: 남은 Track의 마지막 관측 박스 ↔ 남은 high 검출
    if (!unmatched_dets.empty() && !unmatched_trks.empty())
    {
        std::vector<std::vector<double>> iou_left(unmatched_dets.size(), std::vector<double>(unmatched_trks.size()));
        double best = -1.0;
        for (size_t i = 0; i < unmatched_dets.size(); ++i)
            for (size_t j = 0; j < unmatched_trks.size(); ++j)
            {
                iou_left[i][j] = iou(dets[unmatched_dets[i]].data(), last_boxes[unmatched_trks[j]].data());
                best = std::max(best, iou_left[i][j]);
            }
        if (best > config_.iou_threshold)
        {
            std::vector<std::vector<double>> cost = iou_left;
            for (auto& row : cost)
                for (auto& v : row)
                    v = -v;
            std::vector<int> removed_d, removed_t;
            for (const auto& [i, j] : assign_min_cost(cost))
            {
                if (iou_left[i][j] < config_.iou_threshold)
                    continue;
                const int d = unmatched_dets[i], t = unmatched_trks[j];
                update(trackers_[t], &dets[d], det_cls[d]);
                removed_d.push_back(d);
                removed_t.push_back(t);
            }
            unmatched_dets = setdiff(unmatched_dets, removed_d);
            unmatched_trks = setdiff(unmatched_trks, removed_t);
        }
    }

    for (int t : unmatched_trks)
        update(trackers_[t], nullptr, trackers_[t].class_id);

    // 남은 high 검출로 새 Track 생성
    for (int d : unmatched_dets)
        trackers_.push_back(create_track(dets[d], det_cls[d]));

    // 출력: 이번 프레임에 매칭됐고 연속 매칭이 충분한 Track. 박스는 마지막 관측(없으면 Kalman 상태)
    std::vector<Track> out;
    std::vector<OCTrack> alive;
    for (auto& t : trackers_)
    {
        if (t.time_since_update < 1 && (t.hit_streak >= config_.min_hits || frame_count_ <= config_.min_hits))
        {
            std::array<double, 4> b;
            if (t.has_last_observation)
                b = {t.last_observation[0], t.last_observation[1], t.last_observation[2], t.last_observation[3]};
            else
                b = state_box(t.x);
            Track r;
            r.track_id = t.id + 1;
            r.x = static_cast<float>(b[0]);
            r.y = static_cast<float>(b[1]);
            r.width = static_cast<float>(b[2] - b[0]);
            r.height = static_cast<float>(b[3] - b[1]);
            r.class_id = t.class_id;
            r.confidence = static_cast<float>(t.score);
            out.push_back(r);
        }
        if (t.time_since_update <= config_.max_age)
            alive.push_back(std::move(t));
    }
    trackers_ = std::move(alive);
    return out;
}
