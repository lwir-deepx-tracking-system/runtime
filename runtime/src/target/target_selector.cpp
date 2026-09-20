#include "target/target_selector.hpp"

// GUI에서 선택한 ID를 worker가 읽을 수 있도록 갱신한다.
void TargetSelector::set_selected_id(int track_id)
{
    selected_id_.store(track_id);
}

// 현재 프레임의 Track 목록에서 선택 ID와 일치하는 결과를 찾는다.
TargetSelection TargetSelector::select(const std::vector<Track>& tracks) const
{
    TargetSelection selection;
    selection.selected_id = selected_id_.load();

    if (selection.selected_id < 0)
        return selection;

    for (const Track& track : tracks)
    {
        if (track.track_id == selection.selected_id)
        {
            selection.matched_track = track;
            break;
        }
    }

    return selection;
}
