#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/track.hpp"
#include "config/app_config.hpp"

struct ReidMatchEvent
{
    int tracker_id = -1;
    int stable_id = -1;
    float similarity = 0.0F;
};

struct ReidMatchResult
{
    std::vector<Track> tracks;
    std::vector<ReidMatchEvent> matches;
    struct Diagnostic {
        int tracker_id = -1;
        int stable_id = -1;
        std::string reason;
        float similarity = 0;
    };
    std::vector<Diagnostic> diagnostics;
};

// Tracker의 원본 ID를 보존하면서, 새 사람 ID를 최근에 사라진 ID에 1:1로 재연결한다.
class ReidMatcher
{
public:
    explicit ReidMatcher(ReidMatcherConfig config, int embedding_dimensions);
    void reset();
    bool needs_decision_feature(int tracker_id) const;
    ReidMatchResult step(std::uint64_t frame_id, const std::vector<Track>& tracks,
        const std::unordered_map<int, std::vector<float>>& embeddings);

private:
    struct Mapping {
        int stable_id = -1;
        std::uint64_t first_frame = 0;
        std::uint64_t last_frame = 0;
        bool pending = true;
    };
    struct Identity {
        std::uint64_t last_frame = 0;
        Track last_track;
        std::deque<std::vector<float>> gallery;
    };

    ReidMatcherConfig config_;
    int embedding_dimensions_;
    std::uint64_t last_frame_ = 0;
    bool started_ = false;
    int next_fresh_id_ = 1000000;
    std::map<int, Mapping> mappings_;
    std::map<int, Identity> identities_;
    int fresh_id(const std::vector<Track>& tracks);
};
