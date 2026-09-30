#pragma once

#include <pthread.h>

// GUI가 선택한 Track ID를 thread-safe하게 보관한다.
class TargetSelector
{
private:
    // GUI는 set_selected_id()를 통해 선택을 갱신한다. -1은 선택 해제.
    int selected_id_ = -1;
    mutable pthread_mutex_t selected_id_mutex_{};

public:
    TargetSelector();
    ~TargetSelector();

    TargetSelector(const TargetSelector&) = delete;
    TargetSelector& operator=(const TargetSelector&) = delete;

    void set_selected_id(int track_id);
    int get_selected_id() const;
};
