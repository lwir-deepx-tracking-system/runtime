#pragma once

#include <atomic>
#include <optional>
#include <vector>

#include "common/track.hpp"

// 한 프레임에서 선택한 ID가 관측되었는지 나타낸다.
struct TargetSelection
{
    FrameMetadata metadata;
    int selected_id = -1;
    std::optional<Track> matched_track;
};

// GUI가 선택한 ID를 보관하고 현재 Track 목록에서 일치하는 대상을 찾는다.
class TargetSelector
{
private:
    // GUI는 set_selected_id()를 통해 선택을 갱신한다. -1은 선택 해제.
    std::atomic<int> selected_id_{-1};

public:
    void set_selected_id(int track_id);
    // 일치하는 Track이 없으면 matched_track은 비어 있다.
    TargetSelection select(const std::vector<Track>& tracks) const;
};
