#pragma once

// 추적 평가 지표: IDSW, IDF1 (MOTChallenge 방식)
//
// 매칭 기준: 정답 박스와 Track 박스의 IoU가 0.5 이상이면 같은 물체로 본다.
//
// IDSW (ID switches)
//   프레임마다 정답과 Track을 매칭한다. 직전 프레임에 매칭됐던 쌍은 계속 겹치면 우선 유지하고,
//   나머지는 IoU 기준 최적 매칭한다. 어떤 정답 물체가 마지막으로 매칭됐던 Track ID와
//   다른 ID에 매칭되면 IDSW 1회. (물체가 잠시 사라졌다가 다른 ID로 돌아와도 1회)
//
// IDF1
//   영상 전체에서 "정답 물체 하나 ↔ Track ID 하나"를 1:1로 짝지어, 같은 짝으로 맞은
//   프레임 수(IDTP)가 최대가 되게 한다.
//   IDF1 = 2 × IDTP / (정답 박스 수 + Track 출력 수)
//   ID가 한 번 바뀌면 바뀐 뒤 구간은 IDTP에 들어가지 않아 점수가 떨어진다.

#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "common/track.hpp"
#include "tests/tracking/detection_csv.hpp"

namespace tracking_metrics
{

struct Result
{
    int gt_count = 0;    // 전체 정답 박스 수
    int pred_count = 0;  // 전체 Track 출력 수
    int matched = 0;     // 프레임별로 매칭된 정답 박스 수
    int idsw = 0;
    int idtp = 0;
    double idf1 = 0.0;
    std::vector<std::string> idsw_events;  // "frame 46: 정답 1의 ID 1 → 3"
    // 프레임 순서대로 모든 문제 지점: ID 변경, 누락, 헛출력 (-v 출력용)
    std::vector<std::string> timeline;
};

float iou_tlwh(float ax, float ay, float aw, float ah,
                      float bx, float by, float bw, float bh);

// 정사각 비용 행렬의 최소 비용 배정 (Hungarian). 반환: 행 i → 열
std::vector<int> min_cost_assignment(const std::vector<std::vector<double>>& a);

class Accumulator
{
public:
    explicit Accumulator(float iou_threshold = 0.5f);

    // 한 프레임의 정답 박스와 Tracker 출력을 누적한다.
    void add_frame(int frame, const std::vector<test_csv::GtBox>& gt, const std::vector<Track>& tracks);

    // IDF1을 계산해 최종 결과를 돌려준다.
    Result finish() const;

private:
    float threshold_;
    Result result_;
    std::map<int, int> last_match_;                  // 정답 id → 마지막 매칭 Track ID
    std::map<std::pair<int, int>, int> pair_frames_; // (정답 id, Track ID) → 겹친 프레임 수
    std::set<int> gt_ids_, track_ids_;
};

// "IDSW 0 · IDF1 0.988 (IDTP 40 / 정답 41 · 출력 40)"
std::string summary(const Result& r);

}  // namespace tracking_metrics
