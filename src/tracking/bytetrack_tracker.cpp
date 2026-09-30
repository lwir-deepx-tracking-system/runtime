#include "tracking/bytetrack_tracker.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "common/logger.hpp"

using bytetrack::STrack;
using bytetrack::STrackPtr;
using bytetrack::TrackState;

namespace
{

using Box = std::array<float, 4>;  // x, y, width, height

// Kalman 상태에서 현재 bbox를 계산한다.
Box track_box(const STrack& t)
{
    const auto& m = t.kf.mean;
    const float h = static_cast<float>(m[3]);
    const float w = static_cast<float>(m[2] * m[3]);
    return {static_cast<float>(m[0]) - w / 2.0f,
            static_cast<float>(m[1]) - h / 2.0f, w, h};
}

KalmanMeasurement to_xyah(const Box& b)
{
    return {b[0] + b[2] / 2.0, b[1] + b[3] / 2.0,
            static_cast<double>(b[2]) / b[3], b[3]};
}

float iou(const Box& a, const Box& b)
{
    const float x1 = std::max(a[0], b[0]);
    const float y1 = std::max(a[1], b[1]);
    const float x2 = std::min(a[0] + a[2], b[0] + b[2]);
    const float y2 = std::min(a[1] + a[3], b[1] + b[3]);
    const float inter = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
    const float uni = a[2] * a[3] + b[2] * b[3] - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

// cost[i][j] = 1 - IoU(tracks[i]의 예측 bbox, dets[j]의 검출 bbox)
std::vector<std::vector<float>> iou_distance(
    const std::vector<STrackPtr>& tracks,
    const std::vector<STrackPtr>& dets)
{
    std::vector<std::vector<float>> cost(
        tracks.size(), std::vector<float>(dets.size(), 1.0f));
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const Box tb = track_box(*tracks[i]);
        for (size_t j = 0; j < dets.size(); ++j)
            cost[i][j] = 1.0f - iou(tb, dets[j]->det_tlwh);
    }
    return cost;
}

// Hungarian 알고리즘 (n <= m). 반환: 행 i에 배정된 열 번호
std::vector<int> hungarian(const std::vector<std::vector<double>>& a)
{
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(a[0].size());
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
                const double cur = a[i0 - 1][j - 1] - u[i0] - v[j];
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

    std::vector<int> result(n, -1);
    for (int j = 1; j <= m; ++j)
        if (p[j] != 0)
            result[p[j] - 1] = j - 1;
    return result;
}

struct MatchResult
{
    std::vector<std::pair<size_t, size_t>> matches;
    std::vector<size_t> unmatched_rows;
    std::vector<size_t> unmatched_cols;
};

// cost가 thresh 이하인 쌍만 매칭으로 인정한다.
MatchResult linear_assignment(
    const std::vector<std::vector<float>>& cost,
    size_t rows, size_t cols, float thresh)
{
    MatchResult result;
    std::vector<char> row_used(rows, 0), col_used(cols, 0);

    if (rows > 0 && cols > 0)
    {
        const bool transpose = rows > cols;
        const size_t n = transpose ? cols : rows;
        const size_t m = transpose ? rows : cols;
        constexpr double kInvalid = 1e5;

        std::vector<std::vector<double>> a(n, std::vector<double>(m));
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < m; ++j)
            {
                const float c = transpose ? cost[j][i] : cost[i][j];
                a[i][j] = c > thresh ? kInvalid : c;
            }

        const auto assign = hungarian(a);
        for (size_t i = 0; i < n; ++i)
        {
            if (assign[i] < 0)
                continue;
            const size_t j = static_cast<size_t>(assign[i]);
            const size_t r = transpose ? j : i;
            const size_t c = transpose ? i : j;
            if (cost[r][c] > thresh)
                continue;
            result.matches.emplace_back(r, c);
            row_used[r] = 1;
            col_used[c] = 1;
        }
    }

    for (size_t r = 0; r < rows; ++r)
        if (!row_used[r])
            result.unmatched_rows.push_back(r);
    for (size_t c = 0; c < cols; ++c)
        if (!col_used[c])
            result.unmatched_cols.push_back(c);
    return result;
}

// 두 목록을 track_id 기준으로 합친다.
std::vector<STrackPtr> joint(
    const std::vector<STrackPtr>& a, const std::vector<STrackPtr>& b)
{
    std::vector<STrackPtr> out = a;
    std::unordered_set<int> ids;
    for (const auto& t : a)
        ids.insert(t->track_id);
    for (const auto& t : b)
        if (ids.insert(t->track_id).second)
            out.push_back(t);
    return out;
}

// a에서 b에 있는 track_id를 뺀다.
std::vector<STrackPtr> sub(
    const std::vector<STrackPtr>& a, const std::vector<STrackPtr>& b)
{
    std::unordered_set<int> ids;
    for (const auto& t : b)
        ids.insert(t->track_id);
    std::vector<STrackPtr> out;
    for (const auto& t : a)
        if (!ids.count(t->track_id))
            out.push_back(t);
    return out;
}

// tracked와 lost에 거의 같은 위치의 Track이 있으면 짧게 유지된 쪽을 지운다.
void remove_duplicates(
    std::vector<STrackPtr>& tracked, std::vector<STrackPtr>& lost)
{
    std::vector<char> drop_a(tracked.size(), 0), drop_b(lost.size(), 0);
    for (size_t i = 0; i < tracked.size(); ++i)
        for (size_t j = 0; j < lost.size(); ++j)
        {
            if (1.0f - iou(track_box(*tracked[i]), track_box(*lost[j])) >= 0.15f)
                continue;
            const int age_a = tracked[i]->frame_id - tracked[i]->start_frame;
            const int age_b = lost[j]->frame_id - lost[j]->start_frame;
            if (age_a > age_b)
                drop_b[j] = 1;
            else
                drop_a[i] = 1;
        }

    std::vector<STrackPtr> keep_a, keep_b;
    for (size_t i = 0; i < tracked.size(); ++i)
        if (!drop_a[i])
            keep_a.push_back(tracked[i]);
    for (size_t j = 0; j < lost.size(); ++j)
        if (!drop_b[j])
            keep_b.push_back(lost[j]);
    tracked.swap(keep_a);
    lost.swap(keep_b);
}

}  // namespace


ByteTrackTracker::ByteTrackTracker(const TrackingConfig& config)
    : config_(config),
      new_track_threshold_(std::min(config.track_threshold + 0.1F, 1.0F))
{
    max_time_lost_ = config_.track_buffer;

    Logger::info(
        "[Tracking] ByteTrackTracker 생성 완료"
    );

    std::ostringstream values;
    values << "track_threshold=" << config_.track_threshold
           << ", low_threshold=" << kLowThreshold
           << ", new_track_threshold=" << new_track_threshold_
           << ", match_threshold=" << config_.match_threshold
           << ", track_buffer=" << config_.track_buffer
           << " (Lost 유지 " << max_time_lost_ << "프레임)";
    Logger::debug(
        "[Tracking] ByteTrack 설정 값: " + values.str()
    );
}


// 새 Track에 ID를 부여하고 Kalman 상태를 만든다.
void ByteTrackTracker::activate(STrack& t)
{
    t.track_id = ++next_track_id_;
    t.kf = kalman_.initiate(to_xyah(t.det_tlwh));
    t.state = TrackState::Tracked;
    t.is_activated = (frame_id_ == 1);  // 첫 프레임이 아니면 다음 프레임에 확정
    t.tracklet_len = 0;
    t.frame_id = frame_id_;
    t.start_frame = frame_id_;
}


// 매칭된 검출로 Track을 갱신한다. Lost였던 Track도 같은 ID로 되살아난다.
void ByteTrackTracker::apply_detection(STrack& t, const STrack& det)
{
    t.tracklet_len = (t.state == TrackState::Tracked) ? t.tracklet_len + 1 : 0;
    kalman_.update(t.kf, to_xyah(det.det_tlwh));
    t.state = TrackState::Tracked;
    t.is_activated = true;
    t.score = det.score;
    t.class_id = det.class_id;
    t.frame_id = frame_id_;
}


// Detection 목록을 이용해 ByteTrack Tracking을 수행한다.
std::vector<Track> ByteTrackTracker::track(
    const std::vector<Detection>& detections)
{
    ++frame_id_;

    // 1. Detection Confidence 기준 분류
    std::vector<STrackPtr> high_dets, low_dets;
    for (const auto& d : detections)
    {
        if (d.width <= 0.0f || d.height <= 0.0f)
            continue;

        auto det = std::make_shared<STrack>();
        det->det_tlwh = {d.x, d.y, d.width, d.height};
        det->score = d.confidence;
        det->class_id = d.class_id;

        if (d.confidence >= config_.track_threshold)
            high_dets.push_back(std::move(det));
        else if (d.confidence > kLowThreshold)
            low_dets.push_back(std::move(det));
    }

    std::vector<STrackPtr> unconfirmed, confirmed;
    for (const auto& t : tracked_stracks_)
        (t->is_activated ? confirmed : unconfirmed).push_back(t);

    // 2. 기존 Track Prediction (Lost Track 포함)
    std::vector<STrackPtr> pool = joint(confirmed, lost_stracks_);
    for (auto& t : pool)
    {
        if (t->state != TrackState::Tracked)
            t->kf.mean[7] = 0.0;  // 놓친 Track은 높이 변화 속도를 멈춘다
        kalman_.predict(t->kf);
    }

    std::vector<STrackPtr> activated, refind, lost_new, removed;
    auto on_match = [&](const STrackPtr& t, const STrackPtr& det)
    {
        const bool was_tracked = (t->state == TrackState::Tracked);
        apply_detection(*t, *det);
        (was_tracked ? activated : refind).push_back(t);
    };

    // 3-1. 1차 Association: 모든 Track ↔ high detection
    const auto m1 = linear_assignment(
        iou_distance(pool, high_dets), pool.size(), high_dets.size(),
        config_.match_threshold);
    for (const auto& [r, c] : m1.matches)
        on_match(pool[r], high_dets[c]);

    // 3-2. 2차 Association: 남은 Tracked Track ↔ low detection (ByteTrack 핵심)
    std::vector<STrackPtr> remain_tracked;
    for (size_t r : m1.unmatched_rows)
        if (pool[r]->state == TrackState::Tracked)
            remain_tracked.push_back(pool[r]);

    const auto m2 = linear_assignment(
        iou_distance(remain_tracked, low_dets), remain_tracked.size(),
        low_dets.size(), 0.5f);
    for (const auto& [r, c] : m2.matches)
        on_match(remain_tracked[r], low_dets[c]);

    for (size_t r : m2.unmatched_rows)
    {
        auto& t = remain_tracked[r];
        if (t->state != TrackState::Lost)
        {
            t->state = TrackState::Lost;
            lost_new.push_back(t);
        }
    }

    // 3-3. 미확정 Track(직전 프레임에 생긴 Track) ↔ 남은 high detection
    std::vector<STrackPtr> remain_high;
    for (size_t c : m1.unmatched_cols)
        remain_high.push_back(high_dets[c]);

    const auto m3 = linear_assignment(
        iou_distance(unconfirmed, remain_high), unconfirmed.size(),
        remain_high.size(), 0.7f);
    for (const auto& [r, c] : m3.matches)
    {
        apply_detection(*unconfirmed[r], *remain_high[c]);
        activated.push_back(unconfirmed[r]);
    }
    for (size_t r : m3.unmatched_rows)
    {
        unconfirmed[r]->state = TrackState::Removed;
        removed.push_back(unconfirmed[r]);
    }

    // 4. Track 상태 및 ID 갱신
    // 4-1. 매칭되지 않은 high detection으로 새 Track 생성
    for (size_t c : m3.unmatched_cols)
    {
        auto& det = remain_high[c];
        if (det->score < new_track_threshold_)
            continue;
        activate(*det);
        activated.push_back(det);
    }

    // 4-2. 오래 못 찾은 Lost Track 삭제
    for (auto& t : lost_stracks_)
    {
        if (frame_id_ - t->frame_id > max_time_lost_)
        {
            t->state = TrackState::Removed;
            removed.push_back(t);
        }
    }

    // 4-3. 목록 정리
    std::vector<STrackPtr> new_tracked;
    for (const auto& t : tracked_stracks_)
        if (t->state == TrackState::Tracked)
            new_tracked.push_back(t);
    new_tracked = joint(new_tracked, activated);
    new_tracked = joint(new_tracked, refind);

    lost_stracks_ = sub(lost_stracks_, new_tracked);
    lost_stracks_.insert(lost_stracks_.end(), lost_new.begin(), lost_new.end());
    lost_stracks_ = sub(lost_stracks_, removed);

    remove_duplicates(new_tracked, lost_stracks_);
    tracked_stracks_ = std::move(new_tracked);

    // 5. Track 목록 생성 (확정된 Track만 출력)
    std::vector<Track> tracks;
    for (const auto& t : tracked_stracks_)
    {
        if (!t->is_activated)
            continue;
        const Box b = track_box(*t);
        Track out;
        out.track_id = t->track_id;
        out.x = b[0];
        out.y = b[1];
        out.width = b[2];
        out.height = b[3];
        out.class_id = t->class_id;
        out.confidence = t->score;
        tracks.push_back(out);
    }
    return tracks;
}


// ByteTrack이 유지하는 내부 Tracking 상태를 초기화한다.
void ByteTrackTracker::reset()
{
    tracked_stracks_.clear();
    lost_stracks_.clear();
    frame_id_ = 0;
    next_track_id_ = 0;

    Logger::info(
        "[Tracking] ByteTrackTracker 상태 초기화 완료"
    );
}
