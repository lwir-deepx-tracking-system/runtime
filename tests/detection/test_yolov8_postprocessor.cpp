#include "detection/yolov8_postprocessor.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tracking/bytetrack_tracker.hpp"

// YOLOv8 후처리 테스트. NPU 없이 가짜 출력 tensor를 만들어 검사한다.
// 기준 조건: config/model/yolov8n.yaml (입력 640x640, 원본 640x480, class 1개)

namespace
{

int g_failures = 0;

void check(bool ok, const std::string& name)
{
    std::cout << (ok ? "  PASS " : "  FAIL ") << name << "\n";
    if (!ok)
        ++g_failures;
}

bool near(float a, float b, float tol = 0.5f) { return std::fabs(a - b) <= tol; }

ModelPostprocessConfig make_config(int num_classes = 1)
{
    ModelPostprocessConfig c;
    c.confidence_threshold = 0.25f;
    c.nms_threshold = 0.45f;
    c.num_classes = num_classes;
    for (int i = 0; i < num_classes; ++i)
        c.class_names.push_back(i == 0 ? "target" : "class" + std::to_string(i));
    return c;
}

// LwirPreprocessor와 같은 방식(종횡비 유지 + 양쪽 균등 padding)으로 letterbox 정보를 만든다.
LwirPreprocessContext make_context(int orig_w, int orig_h, int in_w = 640, int in_h = 640)
{
    LwirPreprocessContext ctx;
    ctx.original_width = orig_w;
    ctx.original_height = orig_h;
    ctx.input_width = in_w;
    ctx.input_height = in_h;
    ctx.scale = std::min(static_cast<float>(in_w) / orig_w, static_cast<float>(in_h) / orig_h);
    const int rw = static_cast<int>(std::round(orig_w * ctx.scale));
    const int rh = static_cast<int>(std::round(orig_h * ctx.scale));
    ctx.pad_x = (in_w - rw) / 2;
    ctx.pad_y = (in_h - rh) / 2;
    return ctx;
}

// 가짜 YOLOv8 출력 tensor. 기본은 [1, 4 + num_classes, anchors] (채널 우선)
struct FakeOutput
{
    int channels;
    int anchors;
    bool transposed;
    std::vector<float> data;

    FakeOutput(int num_classes, int anchor_count = 8400, bool transpose = false)
        : channels(4 + num_classes), anchors(anchor_count), transposed(transpose),
          data(static_cast<size_t>(channels) * anchor_count, 0.0f) {}

    float& at(int c, int i)
    {
        return transposed ? data[static_cast<size_t>(i) * channels + c]
                          : data[static_cast<size_t>(c) * anchors + i];
    }

    // 원본 영상 TLWH 박스를 모델 입력 좌표(cx, cy, w, h)로 바꿔 anchor i에 넣는다.
    void put(int i, const LwirPreprocessContext& ctx, float x, float y, float w, float h,
             float score, int class_id = 0)
    {
        at(0, i) = (x + w / 2) * ctx.scale + ctx.pad_x;
        at(1, i) = (y + h / 2) * ctx.scale + ctx.pad_y;
        at(2, i) = w * ctx.scale;
        at(3, i) = h * ctx.scale;
        at(4 + class_id, i) = score;
    }

    // 모델 입력 좌표를 그대로 넣는다. (padding 영역 테스트용)
    void put_raw(int i, float cx, float cy, float w, float h, float score)
    {
        at(0, i) = cx; at(1, i) = cy; at(2, i) = w; at(3, i) = h; at(4, i) = score;
    }

    OutputTensorView view() const
    {
        if (transposed)
            return {data.data(), {1, anchors, channels}};
        return {data.data(), {1, channels, anchors}};
    }
};

// ---------------------------------------------------------------------------
void test_letterbox_restore()
{
    std::cout << "[letterbox 역변환]\n";
    const auto ctx = make_context(640, 480);
    check(ctx.scale == 1.0f && ctx.pad_x == 0 && ctx.pad_y == 80,
          "640x480 → 640x640: scale 1.0, pad_y 80");

    Yolov8Postprocessor post(make_config());
    FakeOutput out(1);
    out.put(100, ctx, 100, 200, 40, 80, 0.9f);  // 원본 (100, 200, 40x80)

    const auto dets = post.process(out.view(), ctx);
    check(dets.size() == 1, "박스 1개 검출");
    if (dets.size() == 1)
    {
        const auto& d = dets[0];
        std::cout << "    결과: (" << d.x << ", " << d.y << ", " << d.width << ", " << d.height
                  << ") score " << d.confidence << " class " << d.class_name << "\n";
        check(near(d.x, 100) && near(d.y, 200) && near(d.width, 40) && near(d.height, 80),
              "원본 좌표 (100, 200, 40, 80)로 복원 (TLWH)");
        check(near(d.confidence, 0.9f, 1e-4f) && d.class_id == 0 && d.class_name == "target",
              "점수 0.9, class 0 'target'");
    }

    // 원본이 320x240이면 scale 2.0, pad_y 80
    const auto ctx2 = make_context(320, 240);
    FakeOutput out2(1);
    out2.put(5, ctx2, 50, 60, 20, 30, 0.8f);
    const auto dets2 = post.process(out2.view(), ctx2);
    check(ctx2.scale == 2.0f && dets2.size() == 1 && near(dets2[0].x, 50) && near(dets2[0].y, 60) &&
              near(dets2[0].width, 20) && near(dets2[0].height, 30),
          "320x240 원본(scale 2.0)도 원래 좌표로 복원");
}

// ---------------------------------------------------------------------------
void test_threshold_and_nms()
{
    std::cout << "[점수 필터 · NMS]\n";
    const auto ctx = make_context(640, 480);
    Yolov8Postprocessor post(make_config());

    FakeOutput low(1);
    low.put(0, ctx, 100, 100, 40, 40, 0.20f);  // 0.25 미만
    check(post.process(low.view(), ctx).empty(), "confidence 0.20 < 0.25 박스는 버림");

    FakeOutput dup(1);
    dup.put(0, ctx, 100, 100, 40, 80, 0.90f);
    dup.put(1, ctx, 102, 101, 40, 80, 0.80f);  // 거의 같은 위치
    dup.put(2, ctx, 400, 300, 40, 80, 0.70f);  // 떨어진 위치
    const auto dets = post.process(dup.view(), ctx);
    check(dets.size() == 2, "겹친 박스 2개 중 1개 제거, 떨어진 박스는 유지 (총 2개)");
    check(dets.size() == 2 && near(dets[0].confidence, 0.9f, 1e-4f),
          "겹친 박스 중 점수 높은 0.90이 남음");
}

// ---------------------------------------------------------------------------
void test_clipping()
{
    std::cout << "[경계 처리]\n";
    const auto ctx = make_context(640, 480);
    Yolov8Postprocessor post(make_config());

    FakeOutput edge(1);
    edge.put(0, ctx, -20, 450, 60, 60, 0.9f);  // 왼쪽 아래로 삐져나감
    const auto dets = post.process(edge.view(), ctx);
    check(dets.size() == 1 && near(dets[0].x, 0) && near(dets[0].y, 450) &&
              near(dets[0].width, 40) && near(dets[0].height, 30),
          "영상 밖으로 나간 부분은 잘라냄 → (0, 450, 40, 30)");

    FakeOutput pad(1);
    pad.put_raw(0, 320, 40, 50, 30, 0.9f);  // 위쪽 padding(0~80) 안에만 있는 박스
    check(post.process(pad.view(), ctx).empty(), "padding 영역에만 있는 박스는 버림");
}

// ---------------------------------------------------------------------------
void test_layout_and_errors()
{
    std::cout << "[tensor 형식]\n";
    const auto ctx = make_context(640, 480);
    Yolov8Postprocessor post(make_config());

    FakeOutput t(1, 8400, true);  // [1, 8400, 5]
    t.put(7, ctx, 100, 200, 40, 80, 0.9f);
    const auto dets = post.process(t.view(), ctx);
    check(dets.size() == 1 && near(dets[0].x, 100) && near(dets[0].y, 200),
          "전치 형식 [1, 8400, 5]도 같은 결과");

    FakeOutput multi(3);  // class 3개: 가장 높은 class 선택
    multi.put(0, ctx, 100, 200, 40, 80, 0.3f, 0);
    multi.at(6, 0) = 0.85f;  // class 2 점수
    Yolov8Postprocessor post3(make_config(3));
    const auto m = post3.process(multi.view(), ctx);
    check(m.size() == 1 && m[0].class_id == 2 && near(m[0].confidence, 0.85f, 1e-4f),
          "class가 여러 개면 가장 높은 class(2, 0.85)를 선택");

    std::vector<float> wrong(7 * 100, 0.0f);
    bool thrown = false;
    try
    {
        post.process({wrong.data(), {1, 7, 100}}, ctx);
    }
    catch (const std::invalid_argument& e)
    {
        thrown = true;
        std::cout << "    거부됨: " << e.what() << "\n";
    }
    check(thrown, "num_classes와 맞지 않는 tensor [1, 7, 100]는 오류");

    thrown = false;
    try
    {
        ModelPostprocessConfig bad = make_config();
        bad.num_classes = 0;
        Yolov8Postprocessor p(bad);
    }
    catch (const std::invalid_argument&)
    {
        thrown = true;
    }
    check(thrown, "num_classes 0 설정은 생성 시 오류");
}

// ---------------------------------------------------------------------------
// 후처리 결과를 ByteTrack에 그대로 넣었을 때 ID가 유지되고 좌표가 맞는지 확인한다.
void test_postprocess_to_bytetrack()
{
    std::cout << "[후처리 → ByteTrack 연동]\n";
    const auto ctx = make_context(640, 480);
    Yolov8Postprocessor post(make_config());
    ByteTrackTracker tracker("test-defaults", ByteTrackConfig{});

    bool id_kept = true, coords_ok = true, counts_ok = true;
    int first_id = -1;
    for (int f = 0; f < 10; ++f)
    {
        FakeOutput out(1);
        const float x = 100.0f + 8.0f * f;          // 오른쪽으로 8px/프레임
        out.put(10, ctx, x, 200, 40, 80, 0.9f);
        out.put(11, ctx, x + 1, 200, 40, 80, 0.6f);  // 같은 물체의 중복 박스 → NMS로 제거
        out.put(500, ctx, 500, 50, 30, 30, 0.1f);    // 낮은 점수 → 후처리에서 제거

        const auto dets = post.process(out.view(), ctx);
        const auto tracks = tracker.track(dets);
        if (dets.size() != 1 || tracks.size() != 1)
        {
            counts_ok = false;
            continue;
        }
        if (first_id < 0)
            first_id = tracks[0].track_id;
        if (tracks[0].track_id != first_id)
            id_kept = false;
        if (!near(tracks[0].x, x, 3.0f) || !near(tracks[0].y, 200, 3.0f))
            coords_ok = false;
    }
    check(counts_ok, "매 프레임 검출 1개 → Track 1개 (중복·저점수 박스 제거됨)");
    check(id_kept, "10프레임 동안 같은 ID 유지 (ID " + std::to_string(first_id) + ")");
    check(coords_ok, "Track 좌표가 원본 영상 좌표와 일치 (오차 3px 이내)");
}

}  // namespace


int main()
{
    test_letterbox_restore();
    test_threshold_and_nms();
    test_clipping();
    test_layout_and_errors();
    test_postprocess_to_bytetrack();

    if (g_failures > 0)
    {
        std::cerr << "YOLOv8 후처리 테스트 실패: " << g_failures << "건\n";
        return 1;
    }
    std::cout << "YOLOv8 후처리 테스트 통과\n";
    return 0;
}
