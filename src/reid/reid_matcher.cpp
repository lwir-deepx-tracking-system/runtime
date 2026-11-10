#include "reid/reid_matcher.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
std::vector<float> unit_feature(const std::vector<float>& feature, int dimensions)
{
    if (feature.size() != static_cast<std::size_t>(dimensions))
        throw std::invalid_argument("ReID embedding dimensions do not match matcher");
    double norm_sq = 0.0;
    for (float value : feature)
    {
        if (!std::isfinite(value)) throw std::invalid_argument("ReID embedding is not finite");
        norm_sq += static_cast<double>(value) * value;
    }
    if (!std::isfinite(norm_sq) || norm_sq <= 0.0)
        throw std::invalid_argument("ReID embedding has invalid norm");
    std::vector<float> result = feature;
    const float scale = static_cast<float>(1.0 / std::sqrt(norm_sq));
    for (float& value : result) value *= scale;
    return result;
}

float gallery_similarity(const std::vector<float>& feature,
    const std::deque<std::vector<float>>& gallery)
{
    std::vector<double> mean(feature.size(), 0.0);
    for (const auto& sample : gallery)
        for (std::size_t i = 0; i < mean.size(); ++i) mean[i] += sample[i];
    double norm_sq = 0.0;
    double dot = 0.0;
    for (std::size_t i = 0; i < mean.size(); ++i)
    {
        norm_sq += mean[i] * mean[i];
        dot += mean[i] * feature[i];
    }
    if (norm_sq <= 0.0) return -1.0F;
    return static_cast<float>(dot / std::sqrt(norm_sq));
}

bool valid_box(const Track& track)
{
    return std::isfinite(track.x) && std::isfinite(track.y) &&
        std::isfinite(track.width) && std::isfinite(track.height) &&
        track.width > 0.0F && track.height > 0.0F;
}
}

ReidMatcher::ReidMatcher(ReidMatcherConfig config, int embedding_dimensions)
    : config_(config), embedding_dimensions_(embedding_dimensions)
{
    if (embedding_dimensions_ <= 0 || config_.gallery_size <= 0 ||
        config_.max_lost_frames <= 0 || config_.decision_wait_frames < 0 ||
        config_.decision_wait_frames > config_.max_lost_frames ||
        config_.person_class_id < 0 || !std::isfinite(config_.similarity_threshold) ||
        config_.similarity_threshold < -1.0F || config_.similarity_threshold > 1.0F ||
        !std::isfinite(config_.min_height_ratio) || config_.min_height_ratio <= 0.0F ||
        !std::isfinite(config_.max_height_ratio) ||
        config_.max_height_ratio < config_.min_height_ratio ||
        !std::isfinite(config_.max_center_distance_height) ||
        config_.max_center_distance_height < 0.0F ||
        !std::isfinite(config_.distance_growth_per_10_frames) ||
        config_.distance_growth_per_10_frames < 0.0F)
        throw std::invalid_argument("Invalid ReID matcher configuration");
}

void ReidMatcher::reset()
{
    mappings_.clear();
    identities_.clear();
    last_frame_ = 0;
    started_ = false;
    next_fresh_id_ = 1000000;
}

bool ReidMatcher::needs_decision_feature(int tracker_id) const
{
    const auto it = mappings_.find(tracker_id);
    return it == mappings_.end() || it->second.pending;
}

int ReidMatcher::fresh_id(const std::vector<Track>& tracks)
{
    while (next_fresh_id_ < std::numeric_limits<int>::max())
    {
        const int candidate = next_fresh_id_++;
        if (identities_.count(candidate) != 0) continue;
        if (std::any_of(tracks.begin(), tracks.end(), [candidate](const Track& track) {
                return track.track_id == candidate;
            })) continue;
        return candidate;
    }
    throw std::overflow_error("ReID stable ID space exhausted");
}

ReidMatchResult ReidMatcher::step(std::uint64_t frame_id,
    const std::vector<Track>& tracks,
    const std::unordered_map<int, std::vector<float>>& embeddings)
{
    if (started_ && frame_id <= last_frame_)
        throw std::invalid_argument("ReID frame IDs must increase");
    std::set<int> raw_ids;
    for (const Track& track : tracks)
        if (track.track_id <= 0 || !raw_ids.insert(track.track_id).second)
            throw std::invalid_argument("ReID tracker IDs must be positive and unique");

    std::unordered_map<int, std::vector<float>> features;
    for (const auto& [id, feature] : embeddings)
    {
        if (raw_ids.count(id) == 0)
            throw std::invalid_argument("ReID embedding has no current track");
        features.emplace(id, unit_feature(feature, embedding_dimensions_));
    }

    // 오래된 후보와 원본 ID 매핑을 제거해 장시간 실행 시 상태를 제한한다.
    for (auto it = mappings_.begin(); it != mappings_.end(); )
    {
        if (frame_id > it->second.last_frame &&
            frame_id - it->second.last_frame > static_cast<std::uint64_t>(config_.max_lost_frames))
            it = mappings_.erase(it);
        else ++it;
    }
    for (auto it = identities_.begin(); it != identities_.end(); )
    {
        if (frame_id > it->second.last_frame &&
            frame_id - it->second.last_frame > static_cast<std::uint64_t>(config_.max_lost_frames))
            it = identities_.erase(it);
        else ++it;
    }

    std::set<int> active_stable;
    for (const Track& track : tracks)
        if (track.class_id == config_.person_class_id)
        {
            const auto it = mappings_.find(track.track_id);
            if (it != mappings_.end()) active_stable.insert(it->second.stable_id);
        }

    struct Pair { float score; int raw_id; int stable_id; };
    std::vector<Pair> pairs;
    for (const Track& track : tracks)
    {
        if (track.class_id != config_.person_class_id || track.predicted || !valid_box(track))
            continue;
        const auto feature = features.find(track.track_id);
        if (feature == features.end()) continue;
        const auto mapping = mappings_.find(track.track_id);
        if (mapping != mappings_.end() && !mapping->second.pending) continue;
        if (mapping != mappings_.end() && frame_id - mapping->second.first_frame >
            static_cast<std::uint64_t>(config_.decision_wait_frames)) continue;
        for (const auto& [stable_id, old] : identities_)
        {
            if (active_stable.count(stable_id) || old.gallery.empty() ||
                old.last_frame >= frame_id ||
                frame_id - old.last_frame > static_cast<std::uint64_t>(config_.max_lost_frames) ||
                !valid_box(old.last_track)) continue;
            const float height_ratio = track.height / old.last_track.height;
            if (height_ratio < config_.min_height_ratio ||
                height_ratio > config_.max_height_ratio) continue;
            const float dx = (track.x + track.width * 0.5F) -
                (old.last_track.x + old.last_track.width * 0.5F);
            const float dy = (track.y + track.height * 0.5F) -
                (old.last_track.y + old.last_track.height * 0.5F);
            const float gap = static_cast<float>(frame_id - old.last_frame);
            const float radius = old.last_track.height *
                (config_.max_center_distance_height +
                 config_.distance_growth_per_10_frames * gap / 10.0F);
            if (std::hypot(dx, dy) > radius) continue;
            const float score = gallery_similarity(feature->second, old.gallery);
            if (score >= config_.similarity_threshold)
                pairs.push_back({score, track.track_id, stable_id});
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.raw_id != b.raw_id) return a.raw_id < b.raw_id;
        return a.stable_id < b.stable_id;
    });

    ReidMatchResult result;
    result.tracks = tracks;
    std::map<int, int> assignments;
    std::set<int> taken_raw, taken_stable;
    for (const Pair& pair : pairs)
    {
        if (taken_raw.count(pair.raw_id) || taken_stable.count(pair.stable_id)) continue;
        taken_raw.insert(pair.raw_id);
        taken_stable.insert(pair.stable_id);
        assignments[pair.raw_id] = pair.stable_id;
        result.matches.push_back({pair.raw_id, pair.stable_id, pair.score});
        const auto provisional = mappings_.find(pair.raw_id);
        if (provisional != mappings_.end() &&
            provisional->second.stable_id != pair.stable_id)
            identities_.erase(provisional->second.stable_id);
        // 이전 원본 ID는 재등장 시 새 ID를 받게 한다.
        for (auto it = mappings_.begin(); it != mappings_.end(); )
            if (it->second.stable_id == pair.stable_id && it->first != pair.raw_id)
                it = mappings_.erase(it);
            else ++it;
    }

    for (Track& track : result.tracks)
    {
        if (track.class_id != config_.person_class_id) continue;
        auto it = mappings_.find(track.track_id);
        if (it == mappings_.end())
        {
            int stable_id = track.track_id;
            if (identities_.count(stable_id) || active_stable.count(stable_id))
                stable_id = fresh_id(tracks);
            it = mappings_.emplace(track.track_id,
                Mapping{stable_id, frame_id, frame_id, true}).first;
        }
        if (assignments.count(track.track_id))
            it->second.stable_id = assignments.at(track.track_id);
        it->second.last_frame = frame_id;
        if (features.count(track.track_id) ||
            frame_id - it->second.first_frame >=
                static_cast<std::uint64_t>(config_.decision_wait_frames))
            it->second.pending = false;
        const int stable_id = it->second.stable_id;
        if (!track.predicted && valid_box(track))
        {
            Identity& identity = identities_[stable_id];
            identity.last_frame = frame_id;
            identity.last_track = track;
            const auto feature = features.find(track.track_id);
            if (feature != features.end())
            {
                identity.gallery.push_back(feature->second);
                while (identity.gallery.size() > static_cast<std::size_t>(config_.gallery_size))
                    identity.gallery.pop_front();
            }
        }
        track.track_id = stable_id;
    }
    last_frame_ = frame_id;
    started_ = true;
    return result;
}
