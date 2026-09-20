#include "tracking/bytetrack_tracker.hpp"

#include <iostream>
#include <vector>

int main()
{
    ByteTrackTracker tracker("config/tracking/bytetrack.yaml");

    // TODO: Detection 필드가 확정되면 data/frames.csv를 순서대로 읽고,
    // data/detections.csv의 같은 frame_id 행을 Detection 목록으로 변환한다.
    // 4번 프레임에는 검출 행이 없지만 track({})을 한 번 호출해야 한다.
    // 3번 프레임의 낮은 점수(0.43)와 5번의 재등장도 확인한다.
    // std::vector<Detection> frame1 = { ... };
    // std::vector<Detection> frame2 = { ... };
    // const auto tracks1 = tracker.track(frame1);
    // const auto tracks2 = tracker.track(frame2);
    // 두 결과의 track_id가 유지되는지 검사한다.

    // 검출이 없는 프레임도 빈 목록을 전달해 상태 갱신을 검사한다.
    // const auto missing = tracker.track(std::vector<Detection>{});

    // 아직 ByteTrack 구현이 비어 있으므로 성공한 테스트로 보고하지 않는다.
    (void)tracker;
    std::cerr << "ByteTrack 컴포넌트 테스트는 아직 구현되지 않았습니다\n";
    return 1;
}
