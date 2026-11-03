#include "target/target_selector.hpp"

TargetSelector::TargetSelector()
{
}

TargetSelector::~TargetSelector()
{
}

// GUI에서 선택한 ID를 worker가 읽을 수 있도록 갱신한다.
void TargetSelector::set_selected_id(int track_id)
{
    std::lock_guard(selected_id_mutex_);
    selected_id_ = track_id;
}

// ControlThread가 현재 선택 ID를 조회할 수 있도록 복사해 반환한다.
int TargetSelector::get_selected_id() const
{
    std::lock_guard(selected_id_mutex_);
    const int selected_id = selected_id_;
    return selected_id;
}
