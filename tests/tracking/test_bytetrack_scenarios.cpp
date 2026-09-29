#include "tracking/bytetrack_tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "detection_csv.hpp"

// ByteTrack 시나리오 테스트.
// 데이터: tests/tracking/data/scenarios/<시나리오>/  (generate.py로 재생성 가능)
//   crossing       : 두 물체 교차 시 ID가 뒤바뀌지 않는가
//   false_positive : 한 프레임짜리 오검출에 ID가 출력되지 않는가
//   track_buffer   : track_buffer 안에서는 ID 복구, 넘으면 새 ID인가
//   low_score      : 가려져 점수가 떨어진 구간에도 ID가 유지되는가 (2차 매칭)
//   performance    : 검출 50개 x 300프레임에서 track() 처리 시간 (코드에서 생성)
//
// 사용법:
//   test_bytetrack_scenarios                  전체 시나리오, 요약만 출력 (CTest 기본)
//   test_bytetrack_scenarios -v               전체 시나리오, 프레임별 검출/Track 출력
//   test_bytetrack_scenarios -v crossing      지정한 시나리오만 (여러 개 나열 가능)

namespace
{

using FrameTracks = std::map<int, std::vector<Track>>;

int g_failures = 0;
bool g_verbose = false;

// 한 프레임의 검출과 Track 결과를 한 줄씩 출력한다.
void print_frame(int f, const std::vector<Detection>& dets, const std::vector<Track>& tracks)
{
    std::cout << "    frame " << f << ": detections " << dets.size();
    for (const auto& d : dets)
        std::cout << " [" << d.x << "," << d.y << " s=" << d.confidence << "]";
    std::cout << "\n      -> tracks " << tracks.size();
    for (const auto& t : tracks)
        std::cout << "  id=" << t.track_id << " (" << static_cast<int>(t.x) << ","
                  << static_cast<int>(t.y) << ")";
    std::cout << "\n";
}

void check(bool ok, const std::string& name, const std::string& message)
{
    std::cout << (ok ? "  PASS " : "  FAIL ") << name << ": " << message << "\n";
    if (!ok)
        ++g_failures;
}

bool run(const std::string& name, FrameTracks& out)
{
    test_csv::Sequence seq;
    const std::string dir = test_csv::tracking_test_dir() + "/data/scenarios/" + name;
    const std::string error = test_csv::load_sequence(dir, seq);
    if (!error.empty())
    {
        check(false, name, error);
        return false;
    }

    // YAML 튜닝에 영향받지 않도록 기본값(공식 ByteTrack 값)으로 고정한다.
    ByteTrackTracker tracker("test-defaults", ByteTrackConfig{});
    for (int f : seq.frames)
    {
        out[f] = tracker.track(seq.by_frame[f]);
        if (g_verbose)
            print_frame(f, seq.by_frame[f], out[f]);
    }
    return true;
}

// 점 (px, py)에 중심이 가장 가까운 Track. 반경 안에 없으면 nullptr
const Track* near(const std::vector<Track>& tracks, float px, float py, float radius = 30.0f)
{
    const Track* best = nullptr;
    float best_d = radius;
    for (const auto& t : tracks)
    {
        const float d = std::hypot(t.x + t.width / 2 - px, t.y + t.height / 2 - py);
        if (d < best_d)
        {
            best_d = d;
            best = &t;
        }
    }
    return best;
}

const Track* find_id(const std::vector<Track>& tracks, int id)
{
    for (const auto& t : tracks)
        if (t.track_id == id)
            return &t;
    return nullptr;
}

std::string id_str(const Track* t)
{
    return t ? std::to_string(t->track_id) : std::string("없음");
}

// ---------------------------------------------------------------------------
void test_crossing()
{
    std::cout << "[crossing]\n";
    FrameTracks r;
    if (!run("crossing", r))
        return;

    // 1프레임: A 중심 (120, 240), B 중심 (420, 255)
    const Track* a = near(r[1], 120, 240);
    const Track* b = near(r[1], 420, 255);
    check(a && b, "crossing", "1프레임에서 A=" + id_str(a) + ", B=" + id_str(b));
    if (!a || !b)
        return;
    const int id_a = a->track_id, id_b = b->track_id;

    // 30프레임: A 중심 (410, 240), B 중심 (130, 255)
    const Track* a_end = near(r[30], 410, 240);
    const Track* b_end = near(r[30], 130, 255);
    check(a_end && a_end->track_id == id_a, "crossing",
          "30프레임 A 위치의 ID = " + id_str(a_end) + " (기대 " + std::to_string(id_a) + ")");
    check(b_end && b_end->track_id == id_b, "crossing",
          "30프레임 B 위치의 ID = " + id_str(b_end) + " (기대 " + std::to_string(id_b) + ")");

    std::set<int> all_ids;
    for (const auto& kv : r)
        for (const auto& t : kv.second)
            all_ids.insert(t.track_id);
    check(all_ids.size() == 2, "crossing",
          "전체 사용된 ID 개수 = " + std::to_string(all_ids.size()) + " (기대 2)");
}

// ---------------------------------------------------------------------------
void test_false_positive()
{
    std::cout << "[false_positive]\n";
    FrameTracks r;
    if (!run("false_positive", r))
        return;

    // 오검출 중심 (515, 65): 어느 프레임에서도 출력되면 안 된다.
    bool fp_output = false;
    for (const auto& kv : r)
        if (near(kv.second, 515, 65))
            fp_output = true;
    check(!fp_output, "false_positive", "10프레임 오검출 위치에 Track 출력 없음");

    // 정지 물체 (320, 240)는 전체 구간에서 같은 ID
    const Track* main0 = near(r[1], 320, 240);
    bool main_kept = main0 != nullptr;
    for (const auto& kv : r)
    {
        const Track* t = near(kv.second, 320, 240);
        if (!t || !main0 || t->track_id != main0->track_id)
            main_kept = false;
    }
    check(main_kept, "false_positive", "정지 물체 ID가 1~30프레임 유지");

    // 진짜 새 물체 (100, 380): 20프레임은 미확정, 21프레임부터 같은 ID로 출력
    check(near(r[20], 100, 380) == nullptr, "false_positive",
          "새 물체는 처음 나온 20프레임에는 출력되지 않음 (확정 대기)");
    const Track* n21 = near(r[21], 100, 380);
    bool new_kept = n21 != nullptr;
    for (int f = 21; f <= 30; ++f)
    {
        const Track* t = near(r[f], 100, 380);
        if (!t || !n21 || t->track_id != n21->track_id)
            new_kept = false;
    }
    check(new_kept, "false_positive", "새 물체가 21~30프레임 같은 ID(" + id_str(n21) + ")로 출력");
}

// ---------------------------------------------------------------------------
void test_track_buffer()
{
    std::cout << "[track_buffer]\n";
    FrameTracks r;
    if (!run("track_buffer", r))
        return;

    const Track* a = near(r[1], 120, 240);
    const Track* b = near(r[1], 470, 240);
    check(a && b, "track_buffer", "1프레임에서 A=" + id_str(a) + ", B=" + id_str(b));
    if (!a || !b)
        return;
    const int id_a = a->track_id, id_b = b->track_id;

    check(r[10].empty(), "track_buffer", "물체가 없는 10프레임에는 출력 없음");

    // B: 20프레임 공백 후 26프레임에 돌아옴 → 원래 ID로 즉시 복구
    const Track* b26 = near(r[26], 470, 240);
    check(b26 && b26->track_id == id_b, "track_buffer",
          "B 복구 ID = " + id_str(b26) + " (기대 " + std::to_string(id_b) + ")");

    // A: 40프레임 공백(track_buffer 30 초과) → 새 ID, 확정 후 47프레임부터 출력
    const Track* a47 = near(r[47], 120, 240);
    check(a47 && a47->track_id != id_a, "track_buffer",
          "A 재등장 ID = " + id_str(a47) + " (기존 " + std::to_string(id_a) + "과 달라야 함)");
    check(find_id(r[50], id_a) == nullptr, "track_buffer", "삭제된 A의 옛 ID는 다시 쓰이지 않음");
}

// ---------------------------------------------------------------------------
void test_low_score()
{
    std::cout << "[low_score]\n";
    FrameTracks r;
    if (!run("low_score", r))
        return;

    const Track* first = near(r[1], 120, 240);
    check(first != nullptr, "low_score", "1프레임 ID = " + id_str(first));
    if (!first)
        return;
    const int id = first->track_id;

    int missing = 0;
    for (int f = 1; f <= 25; ++f)
        if (!find_id(r[f], id))
        {
            std::cout << "    " << f << "프레임에 ID " << id << " 없음\n";
            ++missing;
        }
    check(missing == 0, "low_score",
          "점수 0.30 구간(11~15프레임) 포함 1~25프레임 모두 ID " + std::to_string(id) + " 유지");
}

// ---------------------------------------------------------------------------
void test_performance()
{
    std::cout << "[performance]\n";
    constexpr int kObjects = 50;
    constexpr int kFrames = 300;

    std::mt19937 rng(42);  // 고정 시드: 매번 같은 데이터
    std::uniform_real_distribution<float> pos_x(20, 580), pos_y(20, 400);
    std::uniform_real_distribution<float> vel(-3, 3), noise(-1.5f, 1.5f);
    std::uniform_real_distribution<float> score(0.2f, 0.95f), size(15, 40);
    std::uniform_real_distribution<float> unit(0, 1);

    struct Obj { float x, y, vx, vy, w, h; };
    std::vector<Obj> objs;
    for (int i = 0; i < kObjects; ++i)
        objs.push_back({pos_x(rng), pos_y(rng), vel(rng), vel(rng), size(rng), size(rng) * 1.5f});

    // YAML 튜닝에 영향받지 않도록 기본값(공식 ByteTrack 값)으로 고정한다.
    ByteTrackTracker tracker("test-defaults", ByteTrackConfig{});
    std::vector<double> ms;
    size_t max_tracks = 0;

    for (int f = 0; f < kFrames; ++f)
    {
        std::vector<Detection> dets;
        for (auto& o : objs)
        {
            o.x = std::clamp(o.x + o.vx, 0.0f, 600.0f);
            o.y = std::clamp(o.y + o.vy, 0.0f, 420.0f);
            if (unit(rng) < 0.05f)  // 5% 확률로 검출 누락
                continue;
            Detection d;
            d.x = o.x + noise(rng);
            d.y = o.y + noise(rng);
            d.width = o.w;
            d.height = o.h;
            d.confidence = score(rng);
            d.class_id = 0;
            dets.push_back(d);
        }

        const auto t0 = std::chrono::steady_clock::now();
        const auto tracks = tracker.track(dets);
        const auto t1 = std::chrono::steady_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        max_tracks = std::max(max_tracks, tracks.size());
    }

    double sum = 0;
    for (double v : ms)
        sum += v;
    std::sort(ms.begin(), ms.end());
    std::cout << "  검출 " << kObjects << "개 x " << kFrames << "프레임: 평균 "
              << sum / ms.size() << " ms, p99 " << ms[ms.size() * 99 / 100]
              << " ms, 최대 " << ms.back() << " ms, 최대 Track 수 " << max_tracks << "\n";

    // 처리 시간은 PC마다 다르므로 실패 조건으로 쓰지 않는다. (30fps 예산 = 33 ms)
    check(max_tracks <= static_cast<size_t>(kObjects) + 5, "performance",
          "Track 수가 물체 수를 크게 넘지 않음 (" + std::to_string(max_tracks) + ")");
}

}  // namespace


int main(int argc, char** argv)
{
    const std::vector<std::pair<std::string, void (*)()>> scenarios = {
        {"crossing", test_crossing},
        {"false_positive", test_false_positive},
        {"track_buffer", test_track_buffer},
        {"low_score", test_low_score},
        {"performance", test_performance},
    };

    std::set<std::string> selected;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "-v")
            g_verbose = true;
        else
            selected.insert(arg);
    }

    for (const auto& name : selected)
    {
        bool known = false;
        for (const auto& s : scenarios)
            known = known || s.first == name;
        if (!known)
        {
            std::cerr << "알 수 없는 시나리오: " << name
                      << " (crossing, false_positive, track_buffer, low_score, performance)\n";
            return 1;
        }
    }

    for (const auto& s : scenarios)
        if (selected.empty() || selected.count(s.first))
            s.second();

    if (g_failures > 0)
    {
        std::cerr << "ByteTrack 시나리오 테스트 실패: " << g_failures << "건\n";
        return 1;
    }
    std::cout << "ByteTrack 시나리오 테스트 통과\n";
    return 0;
}
