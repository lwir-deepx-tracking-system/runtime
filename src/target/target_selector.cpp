#include "target/target_selector.hpp"

TargetSelector::TargetSelector()
{
    pthread_mutex_init(&selected_id_mutex_, nullptr);
}

TargetSelector::~TargetSelector()
{
    pthread_mutex_destroy(&selected_id_mutex_);
}

// GUI에서 선택한 ID를 worker가 읽을 수 있도록 갱신한다.
void TargetSelector::set_selected_id(int track_id)
{
    pthread_mutex_lock(&selected_id_mutex_);
    selected_id_ = track_id;
    pthread_mutex_unlock(&selected_id_mutex_);
}

// 현재 프레임의 Track 목록에서 선택 ID와 일치하는 결과를 찾는다.
TargetSelection TargetSelector::select(const std::vector<Track>& tracks) const
{
    TargetSelection selection;
    pthread_mutex_lock(&selected_id_mutex_);
    selection.selected_id = selected_id_;
    pthread_mutex_unlock(&selected_id_mutex_);

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
