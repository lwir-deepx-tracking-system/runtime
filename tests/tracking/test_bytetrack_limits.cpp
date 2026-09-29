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

// ByteTrack 한계 케이스 테스트 (성능 기록용)
//
// tests/tracking/data/limits/ 아래 40개 케이스(generate_limits.py로 생성)를 실행한다.
// ByteTrack이 원래 약한 상황이라, 합격 기준 대신 두 값과 비교한다.
//   ideal    (expected.txt) : 완벽한 Tracker라면 나올 값. IDSW 0, 피할 수 없는 확정 대기만 누락
//   baseline (baseline.txt) : 지금 ByteTrack이 낸 값
//
// 판정
//   IDEAL      이상적인 값과 같음
//   WEAK       이상적인 값보다 나쁘지만 기준선과 같음 (알려진 한계)
//   IMPROVED   기준선보다 좋아짐 → --update-baseline 으로 새 기준선 기록 권장
//   REGRESSED  기준선보다 나빠짐 → 테스트 실패
// 비교 순서: IDSW가 먼저, 같으면 누락(miss) 수
//
// 사용법
//   test_bytetrack_limits                     전체 실행, 요약 표
//   test_bytetrack_limits gr_                 이름에 "gr_"가 들어간 케이스만
//   test_bytetrack_limits -v fd_02            한 케이스를 프레임별로 출력
//   test_bytetrack_limits --update-baseline   현재 결과를 baseline.txt에 기록

namespace fs = std::filesystem;

namespace
{

struct Score
{
    int idsw = -1;
    int missed = -1;
    bool valid() const { return idsw >= 0 && missed >= 0; }
};

// a가 b보다 나쁘면 양수, 좋으면 음수, 같으면 0
int compare(const Score& a, const Score& b)
{
    if (a.idsw != b.idsw)
        return a.idsw - b.idsw;
    return a.missed - b.missed;
}

Score read_score(const fs::path& file, const std::string& prefix)
{
    Score s;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.rfind(prefix + "_idsw=", 0) == 0)
            s.idsw = std::stoi(line.substr(prefix.size() + 6));
        else if (line.rfind(prefix + "_missed=", 0) == 0)
            s.missed = std::stoi(line.substr(prefix.size() + 8));
    }
    return s;
}

struct CaseResult
{
    std::string name;
    Score actual, ideal, baseline;
    double idf1 = 0.0;
    std::string verdict;
    std::string error;
};

void print_frame(int f, const std::vector<Detection>& dets, const std::vector<Track>& tracks)
{
    std::cout << "    frame " << f << ": detections " << dets.size();
    for (const auto& d : dets)
        std::cout << " [" << static_cast<int>(d.x) << "," << static_cast<int>(d.y) << " "
                  << static_cast<int>(d.width) << "x" << static_cast<int>(d.height) << "]";
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
    r.ideal = read_score(dir / "expected.txt", "ideal");
    r.baseline = read_score(dir / "baseline.txt", "baseline");

    test_csv::Sequence seq;
    r.error = test_csv::load_sequence(dir.string(), seq);
    if (r.error.empty() && !seq.has_gt)
        r.error = "gt_id 열이 없습니다";
    if (r.error.empty() && !r.ideal.valid())
        r.error = "expected.txt에 ideal_idsw 또는 ideal_missed가 없습니다";
    if (!r.error.empty())
        return r;

    if (verbose)
        std::cout << "[" << r.name << "]\n";

    // YAML 튜닝에 영향받지 않도록 기본값(공식 ByteTrack 값)으로 고정한다.
    ByteTrackTracker tracker("test-defaults", ByteTrackConfig{});
    tracking_metrics::Accumulator metrics;
    for (int f : seq.frames)
    {
        const auto tracks = tracker.track(seq.by_frame[f]);
        metrics.add_frame(f, seq.gt_by_frame[f], tracks);
        if (verbose)
            print_frame(f, seq.by_frame[f], tracks);
    }
    const auto m = metrics.finish();
    r.actual = {m.idsw, m.gt_count - m.matched};
    r.idf1 = m.idf1;
    if (verbose)
        for (const auto& e : m.idsw_events)
            std::cout << "    ID 변경: " << e << "\n";

    if (compare(r.actual, r.ideal) <= 0)
        r.verdict = "IDEAL";
    else if (!r.baseline.valid())
        r.verdict = "NEW";  // 기준선이 아직 없음
    else if (compare(r.actual, r.baseline) > 0)
        r.verdict = "REGRESSED";
    else if (compare(r.actual, r.baseline) < 0)
        r.verdict = "IMPROVED";
    else
        r.verdict = "WEAK";
    return r;
}

}  // namespace


int main(int argc, char** argv)
{
    bool verbose = false, update = false;
    std::vector<std::string> filters;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "-v")
            verbose = true;
        else if (arg == "--update-baseline")
            update = true;
        else
            filters.push_back(arg);
    }

    const fs::path root = fs::path(test_csv::tracking_test_dir()) / "data" / "limits";
    std::vector<fs::path> dirs;
    if (fs::is_directory(root))
        for (const auto& entry : fs::directory_iterator(root))
            if (entry.is_directory() && fs::exists(entry.path() / "detections.csv"))
            {
                const std::string name = entry.path().filename().string();
                bool keep = filters.empty();
                for (const auto& f : filters)
                    keep = keep || name.find(f) != std::string::npos;
                if (keep)
                    dirs.push_back(entry.path());
            }
    std::sort(dirs.begin(), dirs.end());
    if (dirs.empty())
    {
        std::cerr << "실행할 케이스가 없습니다: " << root << "\n";
        return 1;
    }

    std::vector<CaseResult> results;
    for (const auto& d : dirs)
        results.push_back(run_case(d, verbose));

    std::printf("\n[ByteTrack 한계 케이스 %zu개]  (idsw, miss) 비교. ideal = 완벽한 Tracker, base = 현재 기준선\n",
                results.size());
    std::printf("  %-7s %11s %11s %11s %7s  %s\n", "case", "actual", "ideal", "base", "IDF1", "verdict");
    std::printf("  %s\n", std::string(62, '-').c_str());

    std::map<std::string, int> counts;
    std::map<std::string, std::pair<int, int>> by_group;  // 항목 → (IDEAL 수, 전체)
    int failures = 0;
    auto fmt = [](const Score& s) {
        return s.valid() ? "(" + std::to_string(s.idsw) + ", " + std::to_string(s.missed) + ")"
                         : std::string("-");
    };
    for (const auto& r : results)
    {
        if (!r.error.empty())
        {
            ++failures;
            std::printf("  %-7s  ERROR: %s\n", r.name.c_str(), r.error.c_str());
            continue;
        }
        ++counts[r.verdict];
        auto& g = by_group[r.name.substr(0, r.name.find('_'))];
        ++g.second;
        if (r.verdict == "IDEAL")
            ++g.first;
        if (r.verdict == "REGRESSED")
            ++failures;
        std::printf("  %-7s %11s %11s %11s %7.3f  %s\n", r.name.c_str(), fmt(r.actual).c_str(),
                    fmt(r.ideal).c_str(), fmt(r.baseline).c_str(), r.idf1, r.verdict.c_str());

        if (update)
        {
            std::ofstream out(root / r.name / "baseline.txt");
            out << "baseline_idsw=" << r.actual.idsw << "\nbaseline_missed=" << r.actual.missed << "\n";
        }
    }

    std::printf("\n  항목별 IDEAL:");
    for (const auto& [group, g] : by_group)
        std::printf("  %s %d/%d", group.c_str(), g.first, g.second);
    std::printf("\n  판정 합계:");
    for (const auto& [v, n] : counts)
        std::printf("  %s %d", v.c_str(), n);
    std::printf("\n\n");

    if (update)
        std::cout << "baseline.txt를 현재 결과로 기록했습니다 (" << results.size() << "개)\n";
    if (counts["IMPROVED"] > 0 && !update)
        std::cout << "기준선보다 좋아진 케이스가 있습니다. --update-baseline 으로 기준선을 갱신하세요\n";
    if (counts["NEW"] > 0 && !update)
        std::cout << "기준선이 없는 케이스가 있습니다. --update-baseline 으로 기록하세요\n";
    if (failures > 0 && !update)
    {
        std::cerr << "ByteTrack 한계 케이스: 기준선보다 나빠짐(REGRESSED) 또는 오류 " << failures << "건\n";
        return 1;
    }
    std::cout << "ByteTrack 한계 케이스 테스트 통과 (기준선 대비 성능 저하 없음)\n";
    return 0;
}
