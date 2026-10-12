#include "tracking/bytetrack_tracker.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "detection_csv.hpp"
#include "tracking_metrics.hpp"

// ByteTrack 한계 탐색 (파라미터 스윕)
//
// 물체 하나가 오른쪽으로 등속 이동하는 60프레임 시퀀스를 조건만 바꿔 반복 실행하고,
// 결과가 이상적인지(IDSW 0, 헛출력 0, 누락 0) 표로 출력한다. ctest에는 등록하지 않는 분석 도구다.
//
//   1. 속도 스윕   : 크기(정사각형 한 변)별로 속도를 1px씩 올리며 이상적으로 추적되는 최대 속도를 찾는다
//   2. 크기 스윕   : 흔들림 σ를 고정하고 크기를 줄이며, 난수를 바꿔 30회씩 돌린 IDEAL 비율을 구한다
//
// 사용법: bench_bytetrack_sweep

namespace
{

struct Outcome
{
    int idsw = 0, false_out = 0, missed = 0;
    bool ideal() const { return idsw == 0 && false_out == 0 && missed == 0; }
};

// 크기 size(px)의 정사각형 물체가 speed(px/프레임)로 이동. jitter는 검출 좌표 흔들림 표준편차(px)
Outcome run(float size, float speed, float jitter, unsigned seed, int frames = 60)
{
    std::mt19937 rng(seed);
    std::normal_distribution<float> n(0.0f, 1.0f);
    ByteTrackTracker tracker("sweep", ByteTrackConfig{});
    tracking_metrics::Accumulator metrics;

    for (int f = 1; f <= frames; ++f)
    {
        // Tracker는 영상 경계를 모르므로 좌표가 640을 넘어도 된다
        const float x = 100.0f + speed * (f - 1);
        const float y = 200.0f;
        Detection d;
        d.x = x + jitter * n(rng);
        d.y = y + jitter * n(rng);
        d.width = std::max(1.5f, size + 0.5f * jitter * n(rng));
        d.height = std::max(1.5f, size + 0.5f * jitter * n(rng));
        d.confidence = 0.9f;
        d.class_id = 0;

        const auto tracks = tracker.track({d});
        metrics.add_frame(f, {{1, x, y, size, size}}, tracks);
    }
    const auto m = metrics.finish();
    return {m.idsw, m.pred_count - m.matched, m.gt_count - m.matched};
}

}  // namespace


int main()
{
    // -------------------------------------------------------------------------
    std::printf("\n[1] 속도 스윕: 흔들림 없음, 60프레임 등속 이동\n");
    std::printf("    크기별로 이상적(IDSW 0·헛출력 0·누락 0)으로 추적되는 최대 속도와 처음 실패하는 속도\n\n");
    std::vector<std::string> speed_lines;
    for (float size : {6.0f, 10.0f, 20.0f, 40.0f, 80.0f})
    {
        float last_ok = -1.0f, first_fail = -1.0f;
        Outcome fail;
        for (float speed = 0.0f; speed <= 2.0f * size; speed += 0.5f)
        {
            const Outcome o = run(size, speed, 0.0f, 1);
            if (o.ideal())
            {
                if (first_fail < 0)
                    last_ok = speed;
            }
            else if (first_fail < 0)
            {
                first_fail = speed;
                fail = o;
            }
        }
        char buf[200];
        std::snprintf(buf, sizeof(buf), "  %4.0fpx  %11.1fpx/F  %13.2f  %15.1fpx/F     (%d, %d, %d)", size,
                      last_ok, last_ok / size, first_fail, fail.idsw, fail.false_out, fail.missed);
        speed_lines.push_back(buf);
    }
    std::printf("  %6s  %14s  %14s  %20s  %s\n", "크기", "최대 성공 속도", "속도/크기", "처음 실패 속도",
                "실패 시 (IDSW, 헛출력, 누락)");
    for (const auto& l : speed_lines)
        std::printf("%s\n", l.c_str());

    // -------------------------------------------------------------------------
    std::printf("\n    → 한계는 절대 속도가 아니라 '속도 ÷ 이동 방향 폭'으로 정해진다 (약 0.66).\n");
    std::printf("\n[2] 크기 스윕: 속도 2px/프레임, 난수를 바꿔 조건마다 30회 실행\n");
    std::printf("    IDEAL 비율과 평균 (IDSW, 헛출력, 누락)\n\n");
    const std::vector<float> jitters = {0.5f, 1.0f, 2.0f};
    std::printf("  %6s", "크기");
    for (float j : jitters)
        std::printf("  %24s", ("흔들림 σ=" + std::to_string(j).substr(0, 3) + "px").c_str());
    std::printf("\n");
    std::vector<std::string> lines;
    for (float size : {4.0f, 5.0f, 6.0f, 8.0f, 10.0f, 12.0f, 16.0f, 20.0f, 30.0f, 40.0f})
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "  %4.0fpx", size);
        std::string line = buf;
        for (float j : jitters)
        {
            int ideal = 0;
            double s_idsw = 0, s_false = 0, s_miss = 0;
            constexpr int kRuns = 30;
            for (unsigned seed = 1; seed <= kRuns; ++seed)
            {
                const Outcome o = run(size, 2.0f, j, seed * 7919u);
                ideal += o.ideal();
                s_idsw += o.idsw;
                s_false += o.false_out;
                s_miss += o.missed;
            }
            std::snprintf(buf, sizeof(buf), "   %3d%%  (%4.1f, %4.1f, %4.1f)", 100 * ideal / kRuns,
                          s_idsw / kRuns, s_false / kRuns, s_miss / kRuns);
            line += buf;
        }
        lines.push_back(line);
    }
    for (const auto& l : lines)
        std::printf("%s\n", l.c_str());
    std::printf("\n    → 크기가 흔들림 σ의 약 12배 이상이면 95%% 이상, 16배 이상이면 100%% 이상적으로 추적된다.\n\n");
    return 0;
}
