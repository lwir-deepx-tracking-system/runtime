#include "tracking/ocsort_tracker.hpp"

#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
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
            d.delta_t != 3 || d.inertia != 0.2F || d.use_byte)
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
        std::cout << "OC-SORT 설정 읽기 성공 (생략 / 지정)\n";
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

}  // namespace


int main()
{
    int failures = 0;
    failures += check_config();
    failures += check_output_rules();
    failures += check_use_byte();
    failures += check_occlusion();
    failures += check_reset();
    if (failures > 0)
    {
        std::cerr << "OC-SORT 테스트 실패: " << failures << "건\n";
        return 1;
    }
    std::cout << "OC-SORT 테스트 통과\n";
    return 0;
}
