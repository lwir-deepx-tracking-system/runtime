#include "tracking/bytetrack_tracker.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "detection_csv.hpp"
#include "tracking_metrics.hpp"

// ByteTrack 케이스 테스트 (데이터 기반)
//
// tests/tracking/data/cases/ 아래 모든 케이스 폴더를 차례로 실행한다.
// 케이스는 generate_cases.py로 만든다. 폴더마다 detections.csv, frames.csv, expected.txt.
//
// 케이스별 합격 기준
//   1. IDSW가 expected.txt의 expected_idsw와 같다
//   2. 놓친 정답 박스 수가 expected_missed와 같다
//      (새 Track 확정 대기 프레임만 놓쳐야 함. 낮은 점수 구간을 놓치면 여기서 걸린다)
//   3. 오검출(gt_id -1) 위치에 Track이 출력되지 않는다 (그 프레임과 다음 프레임, IoU 0.3 이상)
//
// 사용법
//   test_bytetrack_cases                 전체 케이스, 요약 표만 출력
//   test_bytetrack_cases -v cr_tb_03     이름에 "cr_tb_03"이 들어간 케이스를 프레임별로 출력
//   test_bytetrack_cases fp_             이름에 "fp_"가 들어간 케이스만 실행

namespace fs = std::filesystem;

namespace
{

struct CaseResult
{
    std::string name;
    int expected_idsw = -1;
    int expected_missed = -1;
    int missed = 0;
    int fp_outputs = 0;  // 오검출 위치에 출력된 Track 수
    tracking_metrics::Result metrics;
    bool pass = false;
    std::string error;
};

// expected.txt에서 "key=값" 한 줄을 읽는다. 없으면 -1
int read_expected(const fs::path& dir, const std::string& key)
{
    std::ifstream in(dir / "expected.txt");
    std::string line;
    while (std::getline(in, line))
        if (line.rfind(key + "=", 0) == 0)
            return std::stoi(line.substr(key.size() + 1));
    return -1;
}

void print_frame(int f, const std::vector<Detection>& dets, const std::vector<Track>& tracks)
{
    std::cout << "    frame " << f << ": detections " << dets.size();
    for (const auto& d : dets)
        std::cout << " [" << static_cast<int>(d.x) << "," << static_cast<int>(d.y)
                  << " " << static_cast<int>(d.width) << "x" << static_cast<int>(d.height)
                  << " s=" << d.confidence << "]";
    std::cout << "\n      -> tracks " << tracks.size();
    for (const auto& t : tracks)
        std::cout << "  id=" << t.track_id << " (" << static_cast<int>(t.x) << ","
                  << static_cast<int>(t.y) << ")";
    std::cout << "\n";
}

CaseResult run_case(const fs::path& dir, bool verbose)
{
    CaseResult r;
    r.name = dir.filename().string();
    r.expected_idsw = read_expected(dir, "expected_idsw");
    r.expected_missed = read_expected(dir, "expected_missed");

    test_csv::Sequence seq;
    r.error = test_csv::load_sequence(dir.string(), seq);
    if (r.error.empty() && !seq.has_gt)
        r.error = "gt_id 열이 없습니다";
    if (r.error.empty() && (r.expected_idsw < 0 || r.expected_missed < 0))
        r.error = "expected.txt에 expected_idsw 또는 expected_missed가 없습니다";
    if (!r.error.empty())
        return r;

    if (verbose)
        std::cout << "[" << r.name << "]\n";

    // YAML 튜닝에 영향받지 않도록 기본값(공식 ByteTrack 값)으로 고정한다.
    ByteTrackTracker tracker("test-defaults", ByteTrackConfig{});
    tracking_metrics::Accumulator metrics;
    std::map<int, std::vector<Track>> outputs;

    for (int f : seq.frames)
    {
        outputs[f] = tracker.track(seq.by_frame[f]);
        metrics.add_frame(f, seq.gt_by_frame[f], outputs[f]);
        if (verbose)
            print_frame(f, seq.by_frame[f], outputs[f]);
    }
    r.metrics = metrics.finish();
    r.missed = r.metrics.gt_count - r.metrics.matched;

    // 오검출이 나온 프레임과 그다음 프레임에 그 자리에 Track이 출력되면 안 된다.
    for (const auto& [f, fps] : seq.fp_by_frame)
        for (const auto& fp : fps)
            for (int g : {f, f + 1})
                for (const auto& t : outputs[g])
                    if (tracking_metrics::iou_tlwh(fp.x, fp.y, fp.width, fp.height,
                                                   t.x, t.y, t.width, t.height) >= 0.3f)
                    {
                        ++r.fp_outputs;
                        if (verbose)
                            std::cout << "    오검출 위치에 Track 출력: frame " << g
                                      << " id=" << t.track_id << "\n";
                    }

    r.pass = r.metrics.idsw == r.expected_idsw && r.missed == r.expected_missed &&
             r.fp_outputs == 0;
    if (verbose)
        for (const auto& e : r.metrics.idsw_events)
            std::cout << "    ID 변경: " << e << "\n";
    return r;
}

}  // namespace


int main(int argc, char** argv)
{
    bool verbose = false;
    std::vector<std::string> filters;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "-v")
            verbose = true;
        else
            filters.push_back(arg);
    }

    const fs::path root = fs::path(test_csv::tracking_test_dir()) / "data" / "cases";
    std::vector<fs::path> dirs;
    if (fs::is_directory(root))
        for (const auto& entry : fs::directory_iterator(root))
            if (entry.is_directory() && fs::exists(entry.path() / "detections.csv"))
                dirs.push_back(entry.path());
    std::sort(dirs.begin(), dirs.end());

    if (!filters.empty())
    {
        std::vector<fs::path> kept;
        for (const auto& d : dirs)
            for (const auto& f : filters)
                if (d.filename().string().find(f) != std::string::npos)
                {
                    kept.push_back(d);
                    break;
                }
        dirs.swap(kept);
    }

    if (dirs.empty())
    {
        std::cerr << "실행할 케이스가 없습니다: " << root << "\n";
        return 1;
    }

    std::vector<CaseResult> results;
    for (const auto& d : dirs)
        results.push_back(run_case(d, verbose));

    std::printf("\n[ByteTrack 케이스 %zu개]  IoU 0.5 기준\n", results.size());
    std::printf("  FAIL 조건: IDSW != exp, miss != exp, 오검출 위치 출력(FPout) > 0\n");
    std::printf("  %-10s %9s %9s %7s %6s %6s %6s  %s\n",
                "case", "IDSW/exp", "miss/exp", "IDF1", "GT", "pred", "FPout", "result");
    std::printf("  %s\n", std::string(68, '-').c_str());

    int failures = 0;
    std::map<std::string, std::pair<int, int>> by_group;  // 유형 → (통과, 전체)
    for (const auto& r : results)
    {
        const std::string group = r.name.substr(0, r.name.rfind('_'));
        auto& g = by_group[group];
        ++g.second;
        if (!r.error.empty())
        {
            ++failures;
            std::printf("  %-10s  ERROR: %s\n", r.name.c_str(), r.error.c_str());
            continue;
        }
        if (r.pass)
            ++g.first;
        else
            ++failures;
        const auto& m = r.metrics;
        const std::string idsw = std::to_string(m.idsw) + "/" + std::to_string(r.expected_idsw);
        const std::string miss = std::to_string(r.missed) + "/" + std::to_string(r.expected_missed);
        std::printf("  %-10s %9s %9s %7.3f %6d %6d %6d  %s\n", r.name.c_str(), idsw.c_str(),
                    miss.c_str(), m.idf1, m.gt_count, m.pred_count, r.fp_outputs,
                    r.pass ? "PASS" : "FAIL");
    }

    std::printf("\n  유형별 통과:");
    for (const auto& [group, g] : by_group)
        std::printf("  %s %d/%d", group.c_str(), g.first, g.second);
    std::printf("\n\n");

    if (failures > 0)
    {
        std::cerr << "ByteTrack 케이스 테스트 실패: " << failures << "건 (자세히: -v 케이스이름)\n";
        return 1;
    }
    std::cout << "ByteTrack 케이스 테스트 통과\n";
    return 0;
}
