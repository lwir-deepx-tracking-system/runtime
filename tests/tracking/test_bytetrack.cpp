#include "tracking/bytetrack_tracker.hpp"

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "detection_csv.hpp"

// 사용법: test_bytetrack [data 폴더]
// 인자가 없으면 tests/tracking/data/ 폴더를 사용한다.

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : test_csv::tracking_test_dir() + "/data";

    test_csv::Sequence seq;
    const std::string error = test_csv::load_sequence(dir, seq);
    if (!error.empty())
    {
        std::cerr << error << "\n";
        return 1;
    }

    // runtime 기본값과 같은 typed 설정으로 ByteTrack을 생성한다.
    ByteTrackTracker tracker(TrackingConfig{});
    std::map<int, std::set<int>> ids_by_frame;
    int failures = 0;

    for (int f : seq.frames)
    {
        const auto& dets = seq.by_frame[f];  // 없으면 빈 목록
        const auto tracks = tracker.track(dets);

        std::cout << "frame " << f << ": detections " << dets.size()
                  << " -> tracks " << tracks.size();
        for (const auto& t : tracks)
        {
            std::cout << "  id=" << t.track_id << " (" << t.x << ", " << t.y
                      << ", " << t.width << ", " << t.height
                      << ", score " << t.confidence << ")";
            ids_by_frame[f].insert(t.track_id);
        }
        std::cout << "\n";

        // 검사 1: 검출이 없는 프레임에서는 출력 Track이 없어야 한다.
        if (dets.empty() && !tracks.empty())
        {
            std::cerr << "FAIL: frame " << f << " 검출이 없는데 Track이 출력됨\n";
            ++failures;
        }
    }

    // 검사 2: 첫 프레임의 ID 중 하나는 검출이 있는 이후 모든 프레임에서 유지되어야 한다.
    // (낮은 점수 프레임은 2차 매칭으로, 검출이 사라졌다 돌아온 프레임은 Lost 복구로 유지)
    const auto& frames = seq.frames;
    bool kept = false;
    for (int id : ids_by_frame[frames.front()])
    {
        bool all = true;
        for (size_t i = 1; i < frames.size(); ++i)
            if (!seq.by_frame[frames[i]].empty() && !ids_by_frame[frames[i]].count(id))
                all = false;
        if (all)
        {
            kept = true;
            std::cout << "track_id " << id << "가 모든 검출 프레임에서 유지됨\n";
        }
    }
    if (!kept)
    {
        std::cerr << "FAIL: 첫 프레임의 ID가 이후 프레임에서 유지되지 않음\n";
        ++failures;
    }

    // 검사 3: reset() 후에는 ID가 1부터 다시 시작해야 한다.
    tracker.reset();
    const auto again = tracker.track(seq.by_frame[frames.front()]);
    if (!again.empty() && again.front().track_id != 1)
    {
        std::cerr << "FAIL: reset() 후 ID가 1부터 시작하지 않음\n";
        ++failures;
    }

    if (failures > 0)
    {
        std::cerr << "ByteTrack 테스트 실패: " << failures << "건\n";
        return 1;
    }
    std::cout << "ByteTrack 테스트 통과\n";
    return 0;
}
