#include "target/target_selector.hpp"

int main()
{
    TargetSelector selector;

    // 초기값 -1은 선택 해제 상태이다.
    if (selector.get_selected_id() != -1)
        return 1;

    // GUI에서 설정한 ID를 ControlThread가 동일하게 조회할 수 있어야 한다.
    selector.set_selected_id(12);
    if (selector.get_selected_id() != 12)
        return 2;

    selector.set_selected_id(-1);
    if (selector.get_selected_id() != -1)
        return 3;

    return 0;
}
