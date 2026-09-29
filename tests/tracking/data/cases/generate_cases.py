#!/usr/bin/env python3
"""ByteTrack 테스트 케이스 50개 생성기.

실행하면 이 폴더 아래에 케이스 폴더를 다시 만든다.
    python3 tests/tracking/data/cases/generate_cases.py

구성
  기본 유형 4종 x 5개 = 20개
    crossing (cr), false_positive (fp), track_buffer (tb), low_score (ls)
  복합 유형 6종 x 5개 = 30개  (기본 유형 두 가지가 한 영상에서 동시에 발생)
    cr+fp, cr+tb, cr+ls, fp+tb, fp+ls, tb+ls

케이스 폴더마다
  detections.csv : frame_id, x, y, width, height, confidence, class_id, gt_id
                   (x, y는 bbox 좌상단, 640x480 영상 좌표. gt_id -1 = 오검출)
  frames.csv     : 호출할 프레임 순서 (검출이 없는 프레임 포함)
  expected.txt   : expected_idsw=N, expected_missed=M  (테스트의 합격 기준)
  README.txt     : 상황 설명

기대 IDSW는 물체별 공백으로 자동 계산한다.
  물체가 사라졌다 돌아왔을 때 마지막 검출과 재등장 사이가 31프레임 이하면 같은 ID(0회),
  32프레임 이상이면 track_buffer(30)를 넘겨 새 ID(1회).
  경계(29~34)에 걸친 공백은 만들지 않는다.

기대 누락(expected_missed)은 Tracker가 출력하지 못하는 정답 박스 수다.
  새 Track은 다음 프레임에 확정되므로, 1프레임이 아닌 시점에 처음 나타난 물체와
  track_buffer를 넘겨 새 ID로 돌아온 물체는 첫 프레임 1개를 놓친다. 그 외에는 0.
  낮은 점수 구간도 2차 매칭으로 출력되어야 하므로 누락에 포함하지 않는다.
"""
import csv
import math
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 640, 480
HEADER = ["frame_id", "x", "y", "width", "height", "confidence", "class_id", "gt_id"]
TRACK_BUFFER = 30


class Obj:
    """등속 직선 운동하는 물체 하나. hidden 프레임은 검출 없음, low는 {프레임: 점수}."""

    def __init__(self, gid, x, y, w, h, vx=0.0, vy=0.0, start=1, end=None,
                 hidden=(), low=None, score=0.9):
        self.gid, self.x0, self.y0, self.w, self.h = gid, x, y, w, h
        self.vx, self.vy, self.start, self.end = vx, vy, start, end
        self.hidden = set(hidden)
        self.low = dict(low or {})
        self.score = score

    def box(self, f):
        t = f - self.start
        return (self.x0 + self.vx * t, self.y0 + self.vy * t, self.w, self.h)

    def frames(self):
        return [f for f in range(self.start, self.end + 1) if f not in self.hidden]


def rng(a, b):
    return list(range(a, b + 1))


def iou(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    iw = max(0.0, min(ax + aw, bx + bw) - max(ax, bx))
    ih = max(0.0, min(ay + ah, by + bh) - max(ay, by))
    inter = iw * ih
    union = aw * ah + bw * bh - inter
    return inter / union if union > 0 else 0.0


def build(name, n_frames, objs, fps, note, kinds):
    """케이스 하나를 검증하고 파일로 쓴다. 반환: (이름, 기대 IDSW)"""
    for o in objs:
        if o.end is None:
            o.end = n_frames
        vis = o.frames()
        assert vis, name
        # 영상 안에 있어야 한다
        for f in vis:
            x, y, w, h = o.box(f)
            assert 0 <= x and 0 <= y and x + w <= W and y + h <= H, (name, o.gid, f, x, y)
        # 프레임당 이동량은 박스 크기의 35% 이하 (30fps에서 현실적인 속도)
        assert math.hypot(o.vx, o.vy) <= 0.35 * min(o.w, o.h), (name, o.gid)
        # 낮은 점수는 2차 매칭 구간 (0.1 초과 0.5 미만)
        for f, s in o.low.items():
            assert 0.1 < s < 0.5, (name, f, s)
        # 처음 등장·재등장 프레임부터 3프레임은 높은 점수여야 Track이 생성·복구된다
        segments = []
        for f in vis:
            if segments and f == segments[-1][-1] + 1:
                segments[-1].append(f)
            else:
                segments.append([f])
        for seg in segments:
            for f in seg[:3]:
                assert f not in o.low, (name, o.gid, "재등장 직후 낮은 점수", f)
        # 공백 길이는 경계(29~34)를 피한다
        for a, b in zip(segments, segments[1:]):
            gap = b[0] - a[-1]
            assert not (29 <= gap <= 34), (name, o.gid, "경계 공백", gap)

    # 오검출은 주변 ±35프레임의 모든 실제 물체와 겹치지 않아야 한다
    for (f, x, y, w, h, s) in fps:
        assert 0.6 <= s <= 1.0, (name, "FP 점수는 새 Track 기준(0.6) 이상")
        for o in objs:
            for g in o.frames():
                if abs(g - f) <= 35:
                    assert iou((x, y, w, h), o.box(g)) == 0.0, (name, "FP가 물체와 겹침", f, o.gid, g)

    # crossing 유형은 두 물체의 박스가 실제로 겹치는 프레임이 있어야 한다
    max_overlap = 0.0
    if "cr" in kinds:
        for i, a in enumerate(objs):
            for b in objs[i + 1:]:
                for f in set(a.frames()) | set(b.frames()):
                    if a.start <= f <= a.end and b.start <= f <= b.end:
                        max_overlap = max(max_overlap, iou(a.box(f), b.box(f)))
        assert max_overlap > 0.0, (name, "교차하지 않음")

    # 기대 IDSW: 공백이 track_buffer를 넘으면 물체당 1회
    # 기대 누락: 새 Track이 확정을 기다리는 첫 프레임 (1프레임에 등장한 경우 제외)
    expected = 0
    missed = 0
    for o in objs:
        vis = o.frames()
        if vis[0] != 1:
            missed += 1
        for a, b in zip(vis, vis[1:]):
            if b - a > TRACK_BUFFER + 1:
                expected += 1
                missed += 1

    rows = []
    for o in objs:
        for f in o.frames():
            x, y, w, h = o.box(f)
            rows.append([f, x, y, w, h, o.low.get(f, o.score), 0, o.gid])
    for (f, x, y, w, h, s) in fps:
        rows.append([f, x, y, w, h, s, 0, -1])
    rows.sort(key=lambda r: (r[0], r[7]))

    out = os.path.join(HERE, name)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "detections.csv"), "w", newline="") as fh:
        wr = csv.writer(fh, lineterminator="\n")
        wr.writerow(HEADER)
        for r in rows:
            wr.writerow([r[0]] + [f"{v:.1f}" for v in r[1:5]] + [f"{r[5]:.2f}", r[6], r[7]])
    with open(os.path.join(out, "frames.csv"), "w", newline="") as fh:
        fh.write("frame_id\n" + "".join(f"{i}\n" for i in range(1, n_frames + 1)))
    with open(os.path.join(out, "expected.txt"), "w") as fh:
        fh.write(f"expected_idsw={expected}\nexpected_missed={missed}\n")
    with open(os.path.join(out, "README.txt"), "w") as fh:
        fh.write(note.strip() + "\n")
        fh.write(f"\n프레임 {n_frames}개, 물체 {len(objs)}개, 오검출 {len(fps)}개, "
                 f"기대 IDSW {expected}, 기대 누락 {missed}\n")
        if "cr" in kinds:
            fh.write(f"교차 시 최대 IoU {max_overlap:.2f}\n")
    return name, expected, missed


CASES = []


def case(name, n, objs, fps, note):
    kinds = set(name.split("_")[:-1])
    CASES.append(build(name, n, objs, fps, note, kinds))


# =============================================================================
# 기본 유형: crossing (cr)
# =============================================================================
case("cr_01", 70,
     [Obj(1, 60, 200, 40, 80, vx=6), Obj(2, 500, 212, 40, 80, vx=-6)], [],
     "수평 교차, 중간 크기 40x80, 6px/프레임, 세로 12px 어긋남")
case("cr_02", 125,
     [Obj(1, 300, 40, 16, 16, vy=3), Obj(2, 306, 420, 18, 18, vy=-3)], [],
     "수직 교차, 작은 물체 16~18px, 3px/프레임")
case("cr_03", 60,
     [Obj(1, 40, 40, 100, 120, vx=7, vy=4.5), Obj(2, 500, 60, 90, 110, vx=-7, vy=4.5)], [],
     "X자 대각선 교차, 큰 물체 100x120 / 90x110")
case("cr_04", 90,
     [Obj(1, 100, 180, 120, 120, vx=4), Obj(2, 520, 230, 24, 24, vx=-5)], [],
     "크기 차이가 큰 교차: 큰 물체 120x120 안을 작은 물체 24x24가 통과")
case("cr_05", 60,
     [Obj(1, 420, 380, 30, 50, vx=3), Obj(2, 580, 392, 30, 50, vx=-3)], [],
     "영상 오른쪽 아래 가장자리 근처에서 느린 교차 (3px/프레임)")

# =============================================================================
# 기본 유형: false_positive (fp)
# =============================================================================
case("fp_01", 50,
     [Obj(1, 100, 100, 40, 70, vx=4, vy=2)],
     [(20, 560, 20, 12, 12, 0.95)],
     "이동 물체 + 20프레임에 오른쪽 위 구석의 아주 작은 오검출 (12x12, 0.95)")
case("fp_02", 50,
     [Obj(1, 500, 380, 20, 20, vx=-3)],
     [(15, 80, 60, 150, 100, 0.70)],
     "작은 이동 물체 + 15프레임에 큰 오검출 (150x100, 0.70)")
case("fp_03", 40,
     [Obj(1, 320, 240, 50, 50)],
     [(12, 40, 400, 30, 30, 0.90), (12, 560, 60, 40, 60, 0.80)],
     "정지 물체 + 12프레임에 오검출 2개 동시 발생")
case("fp_04", 60,
     [Obj(1, 200, 300, 60, 90, vx=2, vy=-1)],
     [(10, 500, 50, 25, 25, 0.90), (25, 30, 30, 40, 40, 0.75), (40, 560, 400, 35, 35, 0.85)],
     "이동 물체 + 서로 다른 3개 프레임·위치에 오검출")
case("fp_05", 50,
     [Obj(1, 100, 200, 40, 80), Obj(2, 300, 100, 30, 30, vx=2, vy=1)],
     [(18, 605, 220, 30, 60, 0.90), (30, 0, 440, 40, 35, 0.80)],
     "물체 2개 + 영상 가장자리(오른쪽, 왼쪽 아래)에 붙은 오검출")

# =============================================================================
# 기본 유형: track_buffer (tb)
# =============================================================================
case("tb_01", 45,
     [Obj(1, 450, 90, 20, 20, hidden=rng(11, 25))], [],
     "작은 정지 물체가 15프레임 사라졌다 복귀 → 같은 ID")
case("tb_02", 65,
     [Obj(1, 150, 200, 120, 90, hidden=rng(9, 52))], [],
     "큰 정지 물체가 44프레임 사라졌다 복귀 → track_buffer 초과, 새 ID")
case("tb_03", 60,
     [Obj(1, 80, 300, 40, 60, vx=3, hidden=rng(21, 30))], [],
     "3px/프레임 이동 중 10프레임 사라졌다가 이동 경로상에서 복귀 → 같은 ID")
case("tb_04", 85,
     [Obj(1, 100, 100, 50, 50, hidden=rng(11, 35)), Obj(2, 400, 300, 70, 70, hidden=rng(11, 70))], [],
     "물체 2개: A는 25프레임 공백(같은 ID), B는 60프레임 공백(새 ID)")
case("tb_05", 70,
     [Obj(1, 300, 200, 36, 36, vx=1, vy=1, hidden=rng(11, 18) + rng(31, 50))], [],
     "느리게 이동하며 두 번 사라짐 (8프레임, 20프레임) → 같은 ID")

# =============================================================================
# 기본 유형: low_score (ls)
# =============================================================================
case("ls_01", 40,
     [Obj(1, 100, 200, 40, 80, vx=4, low={f: 0.35 for f in rng(12, 19)})], [],
     "이동 물체의 점수가 12~19프레임 0.35로 하락")
case("ls_02", 40,
     [Obj(1, 300, 300, 18, 18, vy=-2, low={f: 0.15 for f in rng(10, 14)})], [],
     "작은 물체(18px) 점수가 0.15까지 하락 (low_threshold 0.1 바로 위)")
case("ls_03", 40,
     [Obj(1, 200, 150, 140, 110, vx=2, vy=1, low={f: 0.30 for f in rng(10, 21) if f % 2 == 0})], [],
     "큰 물체 점수가 10~21프레임 동안 0.9 / 0.3 번갈아 깜빡임")
case("ls_04", 50,
     [Obj(1, 80, 80, 30, 60, vx=3, vy=2, low={f: 0.25 for f in rng(8, 14)}),
      Obj(2, 450, 350, 60, 40, vx=-2, vy=-1, low={f: 0.40 for f in rng(25, 32)})], [],
     "물체 2개가 서로 다른 시점에 점수 하락")
case("ls_05", 60,
     [Obj(1, 60, 380, 50, 50, vx=2, vy=-1, low={f: 0.40 for f in rng(15, 34)})], [],
     "이동 물체가 20프레임 연속 낮은 점수 (0.40)")

# =============================================================================
# 복합 유형: crossing + false_positive (cr_fp)
# =============================================================================
case("cr_fp_01", 70,
     [Obj(1, 60, 200, 40, 80, vx=6), Obj(2, 500, 212, 40, 80, vx=-6)],
     [(38, 560, 40, 20, 20, 0.90)],
     "수평 교차 순간(38프레임)에 멀리서 오검출")
case("cr_fp_02", 90,
     [Obj(1, 200, 40, 30, 50, vy=4), Obj(2, 208, 400, 30, 50, vy=-4)],
     [(45, 500, 300, 60, 60, 0.80), (46, 20, 20, 15, 15, 0.90)],
     "수직 교차 + 교차 직후 연속 2프레임에 서로 다른 위치 오검출")
case("cr_fp_03", 50,
     [Obj(1, 60, 300, 50, 50, vx=5, vy=-3), Obj(2, 500, 315, 50, 50, vx=-5, vy=-3)],
     [(20, 300, 420, 40, 40, 0.95)],
     "비스듬한 교차 + 교차 전 아래쪽 오검출")
case("cr_fp_04", 90,
     [Obj(1, 420, 180, 120, 120, vx=-4), Obj(2, 60, 230, 24, 24, vx=5)],
     [(10, 20, 420, 30, 30, 0.85), (40, 580, 20, 40, 40, 0.90), (70, 600, 430, 30, 40, 0.75)],
     "큰 물체와 작은 물체 교차 + 3개 프레임에 오검출")
case("cr_fp_05", 70,
     [Obj(1, 40, 220, 40, 40, vx=6), Obj(2, 300, 20, 40, 40, vy=6, start=11), Obj(3, 550, 400, 40, 40)],
     [(44, 40, 40, 30, 30, 0.90)],
     "가로·세로로 움직이는 두 물체가 십자로 교차 + 정지 물체 + 교차 순간 오검출")

# =============================================================================
# 복합 유형: crossing + track_buffer (cr_tb)
# =============================================================================
case("cr_tb_01", 70,
     [Obj(1, 60, 200, 40, 80, vx=6), Obj(2, 500, 212, 40, 80, vx=-6, hidden=rng(34, 42))], [],
     "수평 교차 중 B가 9프레임 가려짐 (교차 구간) → 같은 ID")
case("cr_tb_02", 90,
     [Obj(1, 200, 40, 30, 50, vy=4, hidden=rng(20, 30)), Obj(2, 208, 400, 30, 50, vy=-4)], [],
     "수직 교차 전 A가 11프레임 사라졌다가 경로상에서 복귀 → 같은 ID")
case("cr_tb_03", 95,
     [Obj(1, 60, 150, 40, 60, vx=3, hidden=rng(45, 84)), Obj(2, 400, 160, 40, 60, vx=-3)], [],
     "A가 40프레임 사라진 사이 B가 A의 경로를 지나감, A는 새 ID로 복귀 (기대 IDSW 1)")
case("cr_tb_04", 60,
     [Obj(1, 40, 40, 100, 120, vx=7, vy=4.5), Obj(2, 500, 60, 90, 110, vx=-7, vy=4.5, hidden=rng(36, 47))], [],
     "큰 물체 X자 교차 직후 B가 12프레임 사라짐 → 같은 ID")
case("cr_tb_05", 70,
     [Obj(1, 60, 200, 40, 80, vx=6, hidden=rng(36, 40)), Obj(2, 500, 212, 40, 80, vx=-6, hidden=rng(36, 40))], [],
     "교차 순간 두 물체 모두 5프레임 사라짐 (카메라 셔터 보정 FFC 상황) → 같은 ID")

# =============================================================================
# 복합 유형: crossing + low_score (cr_ls)
# =============================================================================
case("cr_ls_01", 70,
     [Obj(1, 60, 200, 40, 80, vx=6), Obj(2, 500, 212, 40, 80, vx=-6, low={f: 0.30 for f in rng(33, 43)})], [],
     "수평 교차 중 뒤에 가려진 B의 점수가 0.30으로 하락")
case("cr_ls_02", 125,
     [Obj(1, 300, 40, 16, 16, vy=3, low={f: 0.35 for f in rng(60, 70)}),
      Obj(2, 306, 420, 18, 18, vy=-3, low={f: 0.35 for f in rng(60, 70)})], [],
     "작은 물체 수직 교차, 겹치는 동안 두 물체 모두 점수 0.35")
case("cr_ls_03", 60,
     [Obj(1, 40, 40, 100, 120, vx=7, vy=4.5, low={f: 0.20 for f in rng(28, 38)}),
      Obj(2, 500, 60, 90, 110, vx=-7, vy=4.5)], [],
     "큰 물체 X자 교차 중 A의 점수가 0.20으로 하락")
case("cr_ls_04", 90,
     [Obj(1, 100, 180, 120, 120, vx=4), Obj(2, 520, 230, 24, 24, vx=-5, low={f: 0.30 for f in rng(40, 55)})], [],
     "작은 물체가 큰 물체를 통과하는 동안 점수 0.30")
case("cr_ls_05", 120,
     [Obj(1, 150, 250, 50, 50, vx=2), Obj(2, 400, 262, 50, 50, vx=-2, low={f: 0.40 for f in rng(52, 71)})], [],
     "느린 교차 (2px/프레임), 교차 전후 20프레임 동안 B 점수 0.40")

# =============================================================================
# 복합 유형: false_positive + track_buffer (fp_tb)
# =============================================================================
case("fp_tb_01", 45,
     [Obj(1, 450, 90, 30, 30, hidden=rng(11, 20))],
     [(15, 100, 350, 40, 40, 0.90)],
     "물체가 10프레임 사라진 사이 멀리서 오검출 → 같은 ID로 복귀")
case("fp_tb_02", 70,
     [Obj(1, 150, 200, 80, 60, hidden=rng(9, 48))],
     [(20, 500, 40, 30, 30, 0.95), (35, 520, 400, 50, 40, 0.80)],
     "물체가 40프레임 사라짐(새 ID) + 공백 중 오검출 2개 (기대 IDSW 1)")
case("fp_tb_03", 50,
     [Obj(1, 300, 300, 40, 60, hidden=rng(15, 26))],
     [(27, 40, 40, 30, 30, 0.90)],
     "물체가 다시 나타나는 프레임에 동시에 다른 곳에서 오검출")
case("fp_tb_04", 60,
     [Obj(1, 100, 100, 40, 40, hidden=rng(20, 39)), Obj(2, 450, 300, 60, 80, vx=-2)],
     [(25, 500, 40, 20, 20, 0.85), (45, 30, 400, 30, 30, 0.90)],
     "물체 2개 중 A가 20프레임 사라짐 + 2개 프레임에 오검출")
case("fp_tb_05", 60,
     [Obj(1, 80, 200, 40, 60, vx=3, hidden=rng(21, 28))],
     [(22, 500, 50, 25, 25, 0.90), (24, 560, 380, 30, 30, 0.80), (26, 400, 420, 35, 30, 0.95)],
     "이동 물체가 8프레임 사라진 동안 오검출 3개 → 같은 ID")

# =============================================================================
# 복합 유형: false_positive + low_score (fp_ls)
# =============================================================================
case("fp_ls_01", 40,
     [Obj(1, 100, 200, 40, 80, vx=4, low={f: 0.35 for f in rng(12, 19)})],
     [(15, 500, 400, 30, 30, 0.90)],
     "점수가 떨어진 구간 중간에 멀리서 오검출")
case("fp_ls_02", 50,
     [Obj(1, 80, 80, 30, 60, vx=3, vy=2, low={f: 0.25 for f in rng(8, 14)}),
      Obj(2, 450, 350, 60, 40, vx=-2, vy=-1, low={f: 0.40 for f in rng(25, 32)})],
     [(10, 560, 40, 30, 30, 0.85), (28, 40, 420, 30, 30, 0.90)],
     "물체 2개의 점수 하락 시점마다 오검출")
case("fp_ls_03", 40,
     [Obj(1, 200, 150, 140, 110, vx=2, vy=1, low={f: 0.30 for f in rng(10, 21) if f % 2 == 0})],
     [(16, 20, 20, 12, 12, 0.95)],
     "큰 물체 점수 깜빡임 + 작은 오검출")
case("fp_ls_04", 40,
     [Obj(1, 300, 300, 18, 18, vy=-2, low={f: 0.15 for f in rng(10, 14)})],
     [(12, 40, 40, 150, 120, 0.70)],
     "작은 물체 점수 0.15 + 큰 오검출")
case("fp_ls_05", 60,
     [Obj(1, 60, 380, 50, 50, vx=2, vy=-1, low={f: 0.40 for f in rng(15, 34)})],
     [(20, 500, 60, 40, 40, 0.90), (30, 560, 200, 30, 50, 0.80)],
     "20프레임 연속 낮은 점수 + 그 사이 오검출 2개")

# =============================================================================
# 복합 유형: track_buffer + low_score (tb_ls)
# =============================================================================
case("tb_ls_01", 50,
     [Obj(1, 200, 200, 40, 60, low={f: 0.30 for f in rng(10, 15)}, hidden=rng(16, 25))], [],
     "점수 하락(6프레임) 후 10프레임 사라졌다 복귀 → 같은 ID")
case("tb_ls_02", 55,
     [Obj(1, 400, 100, 50, 50, vx=-2, vy=1, hidden=rng(11, 22), low={f: 0.35 for f in rng(27, 34)})], [],
     "12프레임 사라졌다 복귀한 뒤 점수 하락 → 같은 ID")
case("tb_ls_03", 70,
     [Obj(1, 100, 300, 60, 60, low={f: 0.25 for f in rng(8, 12)}, hidden=rng(13, 52))], [],
     "점수 하락 후 40프레임 사라짐 → 새 ID (기대 IDSW 1)")
case("tb_ls_04", 60,
     [Obj(1, 80, 80, 40, 40, vx=2, vy=1, low={f: 0.30 for f in rng(15, 22)}),
      Obj(2, 450, 300, 70, 50, hidden=rng(20, 34))], [],
     "A는 점수 하락, B는 15프레임 사라짐 → 둘 다 같은 ID")
case("tb_ls_05", 60,
     [Obj(1, 100, 250, 50, 50, vx=3, low={f: 0.35 for f in rng(10, 14)} | {f: 0.35 for f in rng(24, 28)},
          hidden=rng(15, 20))], [],
     "이동 중 점수 하락 → 6프레임 사라짐 → 복귀 후 다시 점수 하락")


if __name__ == "__main__":
    print(f"생성 완료: {len(CASES)}개 케이스 (기대 IDSW 합계 {sum(c[1] for c in CASES)})")
    for name, e, m in CASES:
        print(f"  {name:10s} expected_idsw={e} expected_missed={m}")
