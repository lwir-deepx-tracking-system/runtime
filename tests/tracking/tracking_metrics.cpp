#include "tests/tracking/tracking_metrics.hpp"

namespace tracking_metrics {

float iou_tlwh(float ax, float ay, float aw, float ah,
                      float bx, float by, float bw, float bh)
{
    const float x1 = std::max(ax, bx), y1 = std::max(ay, by);
    const float x2 = std::min(ax + aw, bx + bw), y2 = std::min(ay + ah, by + bh);
    const float inter = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
    const float uni = aw * ah + bw * bh - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

std::vector<int> min_cost_assignment(const std::vector<std::vector<double>>& a)
{
    const int n = static_cast<int>(a.size());
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> u(n + 1, 0.0), v(n + 1, 0.0);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);
    for (int i = 1; i <= n; ++i)
    {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(n + 1, inf);
        std::vector<char> used(n + 1, 0);
        do
        {
            used[j0] = 1;
            const int i0 = p[j0];
            double delta = inf;
            int j1 = 0;
            for (int j = 1; j <= n; ++j)
            {
                if (used[j])
                    continue;
                const double cur = a[i0 - 1][j - 1] - u[i0] - v[j];
                if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }
            }
            for (int j = 0; j <= n; ++j)
            {
                if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else minv[j] -= delta;
            }
            j0 = j1;
        } while (p[j0] != 0);
        do { const int j1 = way[j0]; p[j0] = p[j1]; j0 = j1; } while (j0 != 0);
    }
    std::vector<int> result(n, -1);
    for (int j = 1; j <= n; ++j)
        if (p[j] != 0)
            result[p[j] - 1] = j - 1;
    return result;
}

Accumulator::Accumulator(float iou_threshold) : threshold_(iou_threshold) {}

void Accumulator::add_frame(int frame, const std::vector<test_csv::GtBox>& gt, const std::vector<Track>& tracks)
{
    result_.gt_count += static_cast<int>(gt.size());
    result_.pred_count += static_cast<int>(tracks.size());
    for (const auto& g : gt)
        gt_ids_.insert(g.id);
    for (const auto& t : tracks)
        track_ids_.insert(t.track_id);

    const size_t ng = gt.size(), nt = tracks.size();
    std::vector<std::vector<float>> iou(ng, std::vector<float>(nt, 0.0f));
    for (size_t i = 0; i < ng; ++i)
        for (size_t j = 0; j < nt; ++j)
        {
            iou[i][j] = iou_tlwh(gt[i].x, gt[i].y, gt[i].width, gt[i].height,
                                 tracks[j].x, tracks[j].y, tracks[j].width, tracks[j].height);
            if (iou[i][j] >= threshold_)
                ++pair_frames_[{gt[i].id, tracks[j].track_id}];  // IDF1용
        }

    // 1) 직전 매칭 유지: 같은 정답-ID 쌍이 계속 겹치면 그대로 매칭
    std::vector<int> gt_match(ng, -1);
    std::vector<char> track_used(nt, 0);
    for (size_t i = 0; i < ng; ++i)
    {
        const auto it = last_match_.find(gt[i].id);
        if (it == last_match_.end())
            continue;
        for (size_t j = 0; j < nt; ++j)
            if (!track_used[j] && tracks[j].track_id == it->second && iou[i][j] >= threshold_)
            {
                gt_match[i] = static_cast<int>(j);
                track_used[j] = 1;
                break;
            }
    }

    // 2) 나머지는 IoU 기준 최적 매칭
    std::vector<size_t> rest_gt, rest_tr;
    for (size_t i = 0; i < ng; ++i)
        if (gt_match[i] < 0)
            rest_gt.push_back(i);
    for (size_t j = 0; j < nt; ++j)
        if (!track_used[j])
            rest_tr.push_back(j);
    if (!rest_gt.empty() && !rest_tr.empty())
    {
        constexpr double kInvalid = 1e6;
        const size_t n = std::max(rest_gt.size(), rest_tr.size());
        std::vector<std::vector<double>> cost(n, std::vector<double>(n, kInvalid));
        for (size_t a = 0; a < rest_gt.size(); ++a)
            for (size_t b = 0; b < rest_tr.size(); ++b)
            {
                const float v = iou[rest_gt[a]][rest_tr[b]];
                if (v >= threshold_)
                    cost[a][b] = 1.0 - v;
            }
        const auto assign = min_cost_assignment(cost);
        for (size_t a = 0; a < rest_gt.size(); ++a)
        {
            const int b = assign[a];
            if (b >= 0 && static_cast<size_t>(b) < rest_tr.size() && cost[a][b] < kInvalid)
                gt_match[rest_gt[a]] = static_cast<int>(rest_tr[b]);
        }
    }

    // 3) IDSW: 정답 물체가 마지막으로 매칭됐던 ID와 다른 ID에 매칭되면 1회
    const std::string at = "frame " + std::to_string(frame) + ": ";
    std::vector<char> track_matched(nt, 0);
    for (size_t i = 0; i < ng; ++i)
    {
        if (gt_match[i] < 0)
        {
            // 누락: 가장 많이 겹친 출력이 있으면 함께 적어 "위치가 어긋남"과 "출력 없음"을 구분한다
            int best = -1;
            for (size_t j = 0; j < nt; ++j)
                if (iou[i][j] > 0.0f && (best < 0 || iou[i][j] > iou[i][best]))
                    best = static_cast<int>(j);
            std::string line = at + "누락   정답 " + std::to_string(gt[i].id) + " (" +
                               std::to_string(static_cast<int>(gt[i].x)) + "," +
                               std::to_string(static_cast<int>(gt[i].y)) + ")";
            if (best >= 0)
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), " - 가장 가까운 출력 ID %d, IoU %.2f (0.5 미만)",
                              tracks[best].track_id, iou[i][best]);
                line += buf;
            }
            else
            {
                line += " - 겹치는 출력 없음";
            }
            result_.timeline.push_back(line);
            continue;
        }
        track_matched[gt_match[i]] = 1;
        ++result_.matched;
        const int gid = gt[i].id;
        const int tid = tracks[gt_match[i]].track_id;
        const auto it = last_match_.find(gid);
        if (it != last_match_.end() && it->second != tid)
        {
            ++result_.idsw;
            result_.idsw_events.push_back(
                "frame " + std::to_string(frame) + ": 정답 " + std::to_string(gid) +
                "의 ID " + std::to_string(it->second) + " → " + std::to_string(tid));
            result_.timeline.push_back(at + "ID변경 정답 " + std::to_string(gid) + "의 ID " +
                                       std::to_string(it->second) + " → " + std::to_string(tid));
        }
        last_match_[gid] = tid;
    }

    // 헛출력: 어떤 정답과도 매칭되지 않은 출력
    for (size_t j = 0; j < nt; ++j)
        if (!track_matched[j])
            result_.timeline.push_back(
                at + "헛출력 ID " + std::to_string(tracks[j].track_id) + " (" +
                std::to_string(static_cast<int>(tracks[j].x)) + "," +
                std::to_string(static_cast<int>(tracks[j].y)) + ")");
}

Result Accumulator::finish() const
{
    Result r = result_;
    const std::vector<int> g(gt_ids_.begin(), gt_ids_.end());
    const std::vector<int> t(track_ids_.begin(), track_ids_.end());
    const size_t n = std::max(g.size(), t.size());
    if (n > 0)
    {
        // IDTP 최대화 = (-IDTP) 최소화
        std::vector<std::vector<double>> cost(n, std::vector<double>(n, 0.0));
        for (size_t a = 0; a < g.size(); ++a)
            for (size_t b = 0; b < t.size(); ++b)
            {
                const auto it = pair_frames_.find({g[a], t[b]});
                if (it != pair_frames_.end())
                    cost[a][b] = -static_cast<double>(it->second);
            }
        const auto assign = min_cost_assignment(cost);
        for (size_t a = 0; a < g.size(); ++a)
        {
            const int b = assign[a];
            if (b >= 0 && static_cast<size_t>(b) < t.size())
                r.idtp += static_cast<int>(-cost[a][b]);
        }
    }
    const int denom = r.gt_count + r.pred_count;
    r.idf1 = denom > 0 ? 2.0 * r.idtp / denom : 1.0;
    return r;
}

std::string summary(const Result& r)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "IDSW %d · IDF1 %.3f (IDTP %d / 정답 %d · 출력 %d)",
                  r.idsw, r.idf1, r.idtp, r.gt_count, r.pred_count);
    return buf;
}

}  // namespace tracking_metrics
