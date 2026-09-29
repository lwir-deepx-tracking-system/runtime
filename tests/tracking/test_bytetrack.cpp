#include "tracking/bytetrack_tracker.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "detection_csv.hpp"

// 사용법: test_bytetrack [data 폴더]
// 인자가 없으면 tests/tracking/data/ 폴더를 사용한다.

namespace
{

// YAML 내용을 임시 파일로 쓰고 load_bytetrack_config()가 예외를 던지는지 확인한다.
bool rejects(const std::string& name, const std::string& yaml)
{
    const auto path = std::filesystem::temp_directory_path() /
                      ("lwir_bytetrack_test_" + name + ".yaml");
    {
        std::ofstream out(path);
        out << yaml;
    }
    bool thrown = false;
    try
    {
        load_bytetrack_config(path.string());
    }
    catch (const std::runtime_error& e)
    {
        thrown = true;
        std::cout << "  거부됨 (" << name << "): " << e.what() << "\n";
    }
    std::filesystem::remove(path);
    if (!thrown)
        std::cerr << "FAIL: 잘못된 설정(" << name << ")을 받아들임\n";
    return thrown;
}

// 저장소의 config/tracking/bytetrack.yaml 읽기와 잘못된 설정 거부를 검사한다.
int check_yaml_config()
{
    int failures = 0;
    const std::string repo_yaml =
        test_csv::tracking_test_dir() + "/../../config/tracking/bytetrack.yaml";

    try
    {
        const ByteTrackConfig c = load_bytetrack_config(repo_yaml);
        std::cout << "bytetrack.yaml 읽기 성공: track_threshold=" << c.track_thresh
                  << ", match_threshold=" << c.match_thresh
                  << ", track_buffer=" << c.track_buffer
                  << ", new_track_threshold=" << c.new_track_thresh << "\n";
        if (c.new_track_thresh < c.track_thresh)
        {
            std::cerr << "FAIL: new_track_threshold 값이 이상함\n";
            ++failures;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: 저장소의 bytetrack.yaml을 읽지 못함: " << e.what() << "\n";
        ++failures;
    }

    // 파일 경로를 통한 생성자도 같은 값을 적용해야 한다.
    try
    {
        ByteTrackTracker from_yaml(repo_yaml);
        const ByteTrackConfig direct = load_bytetrack_config(repo_yaml);
        if (from_yaml.config().track_buffer != direct.track_buffer ||
            from_yaml.config().track_thresh != direct.track_thresh)
        {
            std::cerr << "FAIL: 생성자가 YAML 값을 적용하지 않음\n";
            ++failures;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: YAML 경로로 Tracker 생성 실패: " << e.what() << "\n";
        ++failures;
    }

    const std::string ok = "track_threshold: 0.5\nmatch_threshold: 0.8\ntrack_buffer: 30\n";
    failures += !rejects("missing_key", "track_threshold: 0.5\nmatch_threshold: 0.8\n");
    failures += !rejects("typo_key", ok + "track_treshold: 0.6\n");
    failures += !rejects("out_of_range", "track_threshold: 1.5\nmatch_threshold: 0.8\ntrack_buffer: 30\n");
    failures += !rejects("low_above_track", ok + "low_threshold: 0.7\n");
    failures += !rejects("not_a_number", "track_threshold: high\nmatch_threshold: 0.8\ntrack_buffer: 30\n");
    failures += !rejects("zero_buffer", "track_threshold: 0.5\nmatch_threshold: 0.8\ntrack_buffer: 0\n");

    bool missing_file_rejected = false;
    try
    {
        load_bytetrack_config("/nonexistent/bytetrack.yaml");
    }
    catch (const std::runtime_error&)
    {
        missing_file_rejected = true;
    }
    if (!missing_file_rejected)
    {
        std::cerr << "FAIL: 없는 파일을 받아들임\n";
        ++failures;
    }
    return failures;
}

}  // namespace

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

    // YAML 튜닝에 영향받지 않도록 기본값(공식 ByteTrack 값)으로 고정한다.
    ByteTrackTracker tracker("test-defaults", ByteTrackConfig{});
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

    // 검사 4: bytetrack.yaml 읽기와 잘못된 설정 거부
    failures += check_yaml_config();

    if (failures > 0)
    {
        std::cerr << "ByteTrack 테스트 실패: " << failures << "건\n";
        return 1;
    }
    std::cout << "ByteTrack 테스트 통과\n";
    return 0;
}
