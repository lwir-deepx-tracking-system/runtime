#include "tracking/ocsort_tracker.hpp"

#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <yaml-cpp/yaml.h>

// 단독 OC-SORT Tracker 검사. 사용법: test_ocsort (인자 없음)
// 공식 구현과의 결과 일치는 별도로 대조했고, 여기서는 동작 규칙과 설정 읽기를 확인한다.

namespace
{

Detection make_detection(float x, float y, float w, float h, float confidence)
{
    Detection d;
    d.x = x;
    d.y = y;
    d.width = w;
    d.height = h;
    d.confidence = confidence;
    d.class_id = 0;
    return d;
}

OCSortConfig parse(const std::string& yaml)
{
    return config_parser::load_ocsort_config(YAML::Load(yaml));
}

bool rejects(const std::string& name, const std::string& yaml)
{
    try
    {
        parse(yaml);
    }
    catch (const std::runtime_error& e)
    {
        std::cout << "  거부됨 (" << name << "): " << e.what() << "\n";
        return true;
    }
    std::cerr << "FAIL: 잘못된 설정(" << name << ")을 받아들임\n";
    return false;
}

// 설정: 키를 모두 생략하면 공식 기본값, 지정하면 그대로, 범위를 벗어나면 거부. 반환: 실패 수
int check_config()
{
    int failures = 0;
    try
    {
        const OCSortConfig d = parse("{}");
        if (d.det_threshold != 0.6F || d.iou_threshold != 0.3F || d.max_age != 30 || d.min_hits != 3 ||
            d.delta_t != 3 || d.inertia != 0.2F || d.use_byte || d.buffer_scale != 0.0F ||
            d.buffer_max_px != 0.0F || d.buffer_small_size != 0.0F || d.lost_output_frames != 0)
        {
            std::cerr << "FAIL: 키를 생략했는데 공식 OC-SORT 기본값이 아님\n";
            ++failures;
        }
        const OCSortConfig s = parse("det_threshold: 0.5\niou_threshold: 0.2\nmax_age: 60\nmin_hits: 1\n"
                                     "delta_t: 5\ninertia: 0.4\nuse_byte: true\n");
        if (s.det_threshold != 0.5F || s.iou_threshold != 0.2F || s.max_age != 60 || s.min_hits != 1 ||
            s.delta_t != 5 || s.inertia != 0.4F || !s.use_byte)
        {
            std::cerr << "FAIL: 지정한 OC-SORT 설정이 그대로 읽히지 않음\n";
            ++failures;
        }
        const OCSortConfig x = parse("buffer_scale: 0.5\nbuffer_max_px: 6\nbuffer_small_size: 30\n"
                                     "lost_output_frames: 2\n");
        if (x.buffer_scale != 0.5F || x.buffer_max_px != 6.0F || x.buffer_small_size != 30.0F ||
            x.lost_output_frames != 2)
        {
            std::cerr << "FAIL: 추가 기능 설정(buffer_*, lost_output_frames)이 그대로 읽히지 않음\n";
            ++failures;
        }
        std::cout << "OC-SORT 설정 읽기 성공 (생략 / 지정 / 추가 기능)\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: 올바른 OC-SORT 설정을 읽지 못함: " << e.what() << "\n";
        ++failures;
    }
    failures += !rejects("zero_det", "det_threshold: 0\n");
    failures += !rejects("zero_iou", "iou_threshold: 0\n");
    failures += !rejects("zero_max_age", "max_age: 0\n");
    failures += !rejects("negative_min_hits", "min_hits: -1\n");
    failures += !rejects("zero_delta_t", "delta_t: 0\n");
    failures += !rejects("huge_inertia", "inertia: 1.5\n");
    failures += !rejects("byte_low_det", "use_byte: true\ndet_threshold: 0.1\n");
    failures += !rejects("not_a_bool", "use_byte: maybe\n");
    failures += !rejects("negative_buffer", "buffer_scale: -0.1\n");
    failures += !rejects("huge_buffer", "buffer_scale: 2.5\n");
    failures += !rejects("negative_buffer_px", "buffer_max_px: -1\n");
    failures += !rejects("negative_small_size", "buffer_small_size: -1\n");
    failures += !rejects("negative_lost_output", "lost_output_frames: -1\n");
    failures += !rejects("lost_output_over_age", "max_age: 5\nlost_output_frames: 6\n");
    return failures;
}

// 출력 규칙:
//   - 처음 min_hits(3) 프레임은 바로 출력, 그 뒤 새 물체는 연속 매칭 3회째(4번째 등장)부터 출력
//   - 출력 박스는 Kalman 추정이 아니라 이번 프레임에 매칭된 검출 박스 그대로
// 반환: 실패 수
int check_output_rules()
{
    int failures = 0;
    OCSortTracker tracker{OCSortConfig{}};
    std::vector<bool> late_seen;
    bool box_exact = true;
    for (int f = 1; f <= 14; ++f)
    {
        // 검출이 좌우로 3px씩 흔들려 Kalman 추정과 검출 박스가 확실히 다르게 한다
        const float jitter = (f % 2 == 0) ? 3.0f : -3.0f;
        std::vector<Detection> dets = {make_detection(50.0f + 0.7f * f + jitter, 50.0f, 40.0f, 80.0f, 0.9f)};
        if (f >= 10)
            dets.push_back(make_detection(300.0f, 200.0f, 40.0f, 80.0f, 0.9f));
        const auto out = tracker.track(dets);
        bool late = false;
        for (const auto& t : out)
        {
            late = late || t.x > 200.0f;
            if (t.x < 200.0f && f > 3 && std::abs(t.x - dets[0].x) > 1e-3f)
                box_exact = false;
        }
        if (f >= 10)
            late_seen.push_back(late);
        if (f <= 3 && out.size() != 1)
        {
            std::cerr << "FAIL: 처음 min_hits 프레임에 바로 출력되지 않음 (프레임 " << f << ")\n";
            ++failures;
        }
    }
    std::cout << "출력 규칙 검사: 10프레임에 나타난 물체 출력 (10~14) " << late_seen[0] << late_seen[1] << late_seen[2]
              << late_seen[3] << late_seen[4] << "\n";
    if (late_seen[0] || late_seen[1] || late_seen[2] || !late_seen[3] || !late_seen[4])
    {
        std::cerr << "FAIL: 새 물체가 연속 매칭 3회째(13프레임)부터 출력되지 않음\n";
        ++failures;
    }
    if (!box_exact)
    {
        std::cerr << "FAIL: 출력 박스가 이번 프레임 검출 박스와 다름\n";
        ++failures;
    }
    return failures;
}

// use_byte: 20프레임 하나만 점수 0.3(low)인 물체.
//   끔 : 그 프레임에 매칭이 없어 연속 매칭이 끊기므로 20~22프레임 출력이 없고 23프레임부터 다시 나와야 한다
//   켬 : low 검출로 2차 매칭되어 끊김 없이 출력되어야 한다. 두 경우 모두 ID는 하나여야 한다
// 반환: 실패 수
int check_use_byte()
{
    auto run = [](bool use_byte) {
        OCSortConfig c;
        c.use_byte = use_byte;
        OCSortTracker tracker(c);
        std::string pattern;
        std::set<int> ids;
        for (int f = 1; f <= 24; ++f)
        {
            const auto out =
                tracker.track({make_detection(100.0f + 2.0f * f, 100.0f, 40.0f, 80.0f, f == 20 ? 0.3f : 0.9f)});
            for (const auto& t : out)
                ids.insert(t.track_id);
            if (f >= 19)
                pattern += out.empty() ? '-' : 'O';
        }
        return std::make_pair(pattern, ids.size());
    };
    const auto off = run(false), on = run(true);
    std::cout << "use_byte 검사 (20프레임만 low, 19~24 출력): 끔 " << off.first << ", 켬 " << on.first << "\n";
    int failures = 0;
    if (off.first != "O---OO" || off.second != 1)
    {
        std::cerr << "FAIL: use_byte를 끄면 low 프레임 뒤 min_hits만큼 출력이 쉬어야 함\n";
        ++failures;
    }
    if (on.first != "OOOOOO" || on.second != 1)
    {
        std::cerr << "FAIL: use_byte를 켜면 low 검출로 끊김 없이 이어져야 함\n";
        ++failures;
    }
    return failures;
}

// 가림 복구: 40x80 물체가 4px/프레임으로 가다가 21~30프레임에 가려진다.
//   계속 움직인 경우 : Kalman 예측으로 같은 ID
//   그 자리에 멈춘 경우: 예측은 44px 앞서 IoU가 0이지만 OCR(마지막 관측 박스)로 같은 ID
//   멈춘 경우 + 가려진 동안 카메라가 6px/프레임 회전(CMC 지정): 관측 기록도 옮기므로 같은 ID이고,
//   다시 보인 뒤 출력이 카메라가 가만히 있을 때와 (카메라 이동만큼을 빼면) 같아야 한다
// 반환: 실패 수
int check_occlusion()
{
    // 반환: 사용한 ID 수, 다시 보인 뒤(31~40) 출력 x에서 카메라 이동을 뺀 값
    auto run = [](float speed_after, float pan) {
        OCSortTracker tracker{OCSortConfig{}};
        std::set<int> ids;
        std::vector<float> xs;
        float x = 100.0f, cam = 0.0f;
        for (int f = 1; f <= 40; ++f)
        {
            x += (f <= 20) ? 4.0f : speed_after;
            if (f > 20 && f <= 30 && pan != 0.0f)
            {
                cam += pan;
                tracker.set_camera_motion(CameraMotion::translation(pan, 0.0));
            }
            std::vector<Detection> dets;
            if (f <= 20 || f > 30)
                dets.push_back(make_detection(x + cam, 200.0f, 40.0f, 80.0f, 0.9f));
            for (const auto& t : tracker.track(dets))
            {
                ids.insert(t.track_id);
                if (f > 30)
                    xs.push_back(t.x - cam);
            }
        }
        return std::make_pair(ids.size(), xs);
    };
    const auto moving = run(4.0f, 0.0f), stopped = run(0.0f, 0.0f), panned = run(0.0f, -6.0f);
    std::cout << "가림 복구 검사: 사용한 ID 수 계속 이동 " << moving.first << "개, 멈춤 " << stopped.first
              << "개, 멈춤 + 카메라 회전 " << panned.first << "개\n";
    int failures = 0;
    if (moving.first != 1 || stopped.first != 1 || panned.first != 1)
    {
        std::cerr << "FAIL: 가림 뒤 같은 ID로 이어지지 않음\n";
        ++failures;
    }
    bool same = stopped.second.size() == panned.second.size() && !stopped.second.empty();
    for (size_t i = 0; same && i < stopped.second.size(); ++i)
        same = std::abs(stopped.second[i] - panned.second[i]) < 1e-3f;
    if (!same)
    {
        std::cerr << "FAIL: 가려진 동안 카메라가 움직이면 결과가 달라짐 (CMC 미적용)\n";
        ++failures;
    }
    return failures;
}

// reset() 뒤에는 ID가 1부터 다시 시작해야 한다. 반환: 실패 수
int check_reset()
{
    OCSortTracker tracker{OCSortConfig{}};
    for (int f = 1; f <= 5; ++f)
        tracker.track({make_detection(10.0f * f, 10.0f, 40.0f, 80.0f, 0.9f),
                       make_detection(300.0f, 10.0f * f, 40.0f, 80.0f, 0.9f)});
    tracker.reset();
    const auto out = tracker.track({make_detection(100.0f, 100.0f, 40.0f, 80.0f, 0.9f)});
    if (out.size() != 1 || out.front().track_id != 1)
    {
        std::cerr << "FAIL: reset() 후 ID가 1부터 시작하지 않음\n";
        return 1;
    }
    return 0;
}

// 추가 기능 검사용 기본 설정: LWIR 운용 설정과 같은 det 0.5, iou 0.2, min_hits 1, use_byte
OCSortConfig lwir_config()
{
    OCSortConfig c;
    c.det_threshold = 0.5F;
    c.iou_threshold = 0.2F;
    c.min_hits = 1;
    c.use_byte = true;
    return c;
}

// 작은 박스가 오른쪽으로 3px씩 가다가, 예측 위치보다 폭의 9/8배 더 튄다 (원래 IoU 0, 0.5배씩 넓히면 약 0.28).
// 반환: 튀기 직전 ID와 튄 뒤 ID
std::pair<int, int> jump_ids(const OCSortConfig& c, float w, float h)
{
    OCSortTracker tracker(c);
    float x = 100.0f;
    int before = -1, after = -1;
    for (int f = 0; f < 10; ++f)
    {
        x += 3.0f;
        const auto out = tracker.track({make_detection(x, 200.0f, w, h, 0.9f)});
        before = out.empty() ? -1 : out.front().track_id;
    }
    x += 3.0f + 9.0f * (w / 8.0f);
    for (int f = 0; f < 3; ++f)
    {
        const auto out = tracker.track({make_detection(x, 200.0f, w, h, 0.9f)});
        after = out.empty() ? -1 : out.front().track_id;
        x += 3.0f;
    }
    return {before, after};
}

// Buffered IoU: 꺼지면 공식 동작(튀면 새 ID), 켜면 같은 ID 유지. max_px·small_size 제한과 이웃 혼동도 확인. 반환: 실패 수
int check_buffered_iou()
{
    int failures = 0;
    const auto expect = [&](bool ok, const char* msg) {
        if (!ok)
        {
            std::cerr << "FAIL: Buffered IoU - " << msg << "\n";
            ++failures;
        }
    };
    OCSortConfig c = lwir_config();
    auto ids = jump_ids(c, 8.0f, 20.0f);
    expect(ids.first != ids.second, "꺼져 있는데 튄 뒤에도 같은 ID (공식 동작과 다름)");

    c.buffer_scale = 0.5F;
    ids = jump_ids(c, 8.0f, 20.0f);
    expect(ids.first == ids.second, "켰는데 작은 박스가 튄 뒤 새 ID가 생김");

    OCSortConfig limited = c;
    limited.buffer_max_px = 1.0F;
    ids = jump_ids(limited, 8.0f, 20.0f);
    expect(ids.first != ids.second, "buffer_max_px 1인데 넓히는 양이 제한되지 않음");

    OCSortConfig small_only = c;
    small_only.buffer_small_size = 30.0F;
    ids = jump_ids(small_only, 40.0f, 100.0f);
    expect(ids.first != ids.second, "buffer_small_size 30인데 짧은 변 40px 박스에도 적용됨");
    ids = jump_ids(small_only, 8.0f, 20.0f);
    expect(ids.first == ids.second, "buffer_small_size 30인데 작은 박스에 적용되지 않음");

    // 나란히 걷는 두 사람이 함께 튀어도 각자 원래 ID로 이어져야 한다
    OCSortTracker tracker(c);
    float x = 100.0f;
    std::vector<Track> out;
    for (int f = 0; f < 8; ++f)
    {
        x += 3.0f;
        out = tracker.track({make_detection(x, 200.0f, 8.0f, 20.0f, 0.9f), make_detection(x + 40.0f, 200.0f, 8.0f, 20.0f, 0.9f)});
    }
    const auto left_right = [](const std::vector<Track>& v) {
        return v[0].x < v[1].x ? std::make_pair(v[0].track_id, v[1].track_id) : std::make_pair(v[1].track_id, v[0].track_id);
    };
    const auto before = out.size() == 2 ? left_right(out) : std::make_pair(-1, -1);
    x += 12.0f;
    out = tracker.track({make_detection(x, 200.0f, 8.0f, 20.0f, 0.9f), make_detection(x + 40.0f, 200.0f, 8.0f, 20.0f, 0.9f)});
    expect(out.size() == 2 && left_right(out) == before, "나란히 선 두 사람이 튀었을 때 ID가 바뀌거나 섞임");

    if (failures == 0)
        std::cout << "Buffered IoU 확인 (꺼짐 = 공식 동작, 켬 = 같은 ID 유지, 제한 옵션, 이웃 구분)\n";
    return failures;
}

// 놓친 프레임 출력: 출력된 적 있는 Track만, lost_output_frames 프레임까지, 같은 ID의 예측 박스로 낸다. 반환: 실패 수
int check_lost_output()
{
    int failures = 0;
    const auto expect = [&](bool ok, const char* msg) {
        if (!ok)
        {
            std::cerr << "FAIL: 놓친 프레임 출력 - " << msg << "\n";
            ++failures;
        }
    };
    // 반환: 놓친 3프레임 동안 아래쪽 사람(y=300)이 출력된 횟수
    const auto coasted = [](int lost_output_frames, int min_hits, int seen_frames) {
        OCSortConfig c = lwir_config();
        c.lost_output_frames = lost_output_frames;
        c.min_hits = min_hits;
        OCSortTracker tracker(c);
        for (int f = 0; f < 6; ++f)
            tracker.track({make_detection(100.0f, 200.0f, 8.0f, 20.0f, 0.9f)});
        float x = 100.0f;
        for (int f = 0; f < seen_frames; ++f)
        {
            x += 3.0f;
            tracker.track({make_detection(100.0f, 200.0f, 8.0f, 20.0f, 0.9f), make_detection(x, 300.0f, 10.0f, 30.0f, 0.9f)});
        }
        std::vector<int> counts;
        for (int f = 0; f < 3; ++f)
        {
            int n = 0;
            for (const auto& t : tracker.track({make_detection(100.0f, 200.0f, 8.0f, 20.0f, 0.9f)}))
                n += (t.y > 250.0f);
            counts.push_back(n);
        }
        return counts;
    };
    expect(coasted(0, 1, 5) == std::vector<int>{0, 0, 0}, "꺼져 있는데 놓친 프레임에 출력함");
    expect(coasted(1, 1, 5) == std::vector<int>{1, 0, 0}, "1이면 놓친 첫 프레임만 출력해야 함");
    expect(coasted(2, 1, 5) == std::vector<int>{1, 1, 0}, "2면 놓친 두 프레임까지 출력해야 함");
    expect(coasted(2, 3, 2) == std::vector<int>{0, 0, 0}, "한 번도 출력되지 않은 Track이 예측 박스를 냄");

    OCSortConfig c = lwir_config();
    c.lost_output_frames = 1;
    OCSortTracker tracker(c);
    std::vector<Track> prev;
    float x = 100.0f;
    for (int f = 0; f < 8; ++f)
    {
        x += 3.0f;
        prev = tracker.track({make_detection(x, 300.0f, 10.0f, 30.0f, 0.9f)});
    }
    const auto cur = tracker.track({});
    expect(prev.size() == 1 && cur.size() == 1 && cur[0].track_id == prev[0].track_id && cur[0].x > prev[0].x,
           "예측 박스가 같은 ID가 아니거나 이동 방향으로 앞서 있지 않음");

    if (failures == 0)
        std::cout << "놓친 프레임 출력 확인 (꺼짐 / 1·2프레임 / 출력 이력 조건 / 같은 ID 예측 박스)\n";
    return failures;
}

}  // namespace


int main()
{
    int failures = 0;
    failures += check_config();
    failures += check_output_rules();
    failures += check_use_byte();
    failures += check_occlusion();
    failures += check_reset();
    failures += check_buffered_iou();
    failures += check_lost_output();
    if (failures > 0)
    {
        std::cerr << "OC-SORT 테스트 실패: " << failures << "건\n";
        return 1;
    }
    std::cout << "OC-SORT 테스트 통과\n";
    return 0;
}
