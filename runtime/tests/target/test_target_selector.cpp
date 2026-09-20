#include "target/target_selector.hpp"

#include <vector>

int main()
{
    TargetSelector selector;
    Track first;
    first.track_id = 7;
    Track second;
    second.track_id = 12;
    const std::vector<Track> tracks{first, second};

    // ID를 선택하지 않으면 어떤 Track도 제어 대상으로 내보내지 않는다.
    if (selector.select(tracks).matched_track.has_value())
        return 1;

    // 선택한 ID가 있으면 그 Track만 반환한다.
    selector.set_selected_id(12);
    const TargetSelection selected = selector.select(tracks);
    if (selected.selected_id != 12 || !selected.matched_track ||
        selected.matched_track->track_id != 12)
        return 1;

    // 선택 ID가 현재 프레임에 없으면 매칭 결과가 비어 있어야 한다.
    selector.set_selected_id(99);
    if (selector.select(tracks).matched_track.has_value())
        return 1;
}
