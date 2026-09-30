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

// ControlThread가 현재 선택 ID를 조회할 수 있도록 복사해 반환한다.
int TargetSelector::get_selected_id() const
{
    pthread_mutex_lock(&selected_id_mutex_);
    const int selected_id = selected_id_;
    pthread_mutex_unlock(&selected_id_mutex_);
    return selected_id;
}
