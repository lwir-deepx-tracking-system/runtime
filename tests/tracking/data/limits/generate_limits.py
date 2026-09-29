#!/usr/bin/env python3
"""ByteTrack 한계 케이스 40개 생성기 (8개 항목 x 5개).

실행하면 이 폴더 아래에 케이스 폴더를 다시 만든다. baseline.txt는 건드리지 않는다.
    python3 tests/tracking/data/limits/generate_limits.py

항목
  카메라 움직임  gr (짐벌 회전·팬), gj (짐벌 급출발·급정지·진동)
  프레임 타이밍  fd (프레임 누락),   ff (FFC 셔터 보정으로 영상 정지)
  표적 특성      sm (작은 표적 + 검출 흔들림), mv (급격한 방향 전환),
                 sc (빠른 크기 변화), ed (화면 밖으로 나갔다 재진입)

좌표 모델
  물체는 "세계 좌표"에서 움직이고, 카메라(짐벌)도 세계 좌표에서 움직인다.
  영상 속 박스 = 세계 좌표 - 카메라 위치. 영상(640x480) 밖으로 나간 부분은 잘라내고,
  원래 크기의 25% 미만만 보이면 검출되지 않는 것으로 본다.
  정답(gt) 박스는 영상에 보이는 박스, 검출 박스는 여기에 흔들림(jitter)을 더한 값이다.

케이스 폴더마다
  detections.csv : frame_id, x, y, width, height, confidence, class_id, gt_id
  frames.csv     : Tracker를 호출하는 프레임 (fd 항목은 누락된 프레임이 빠져 있음)
  expected.txt   : ideal_idsw, ideal_false, ideal_missed  (완벽한 Tracker라면 나와야 할 값)
  README.txt     : 상황 설명
  baseline.txt   : 현재 ByteTrack 결과 (test_bytetrack_limits --update-baseline 으로 기록)

이상적인 값
  ideal_idsw   = 0  (같은 물체는 끝까지 같은 ID)
  ideal_false  = 0  (정답과 겹치지 않는 헛출력 없음)
  ideal_missed = 1프레임이 아닌 시점에 처음 나타난 물체 수 (새 Track 확정 대기는 피할 수 없음)
"""
import csv
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 640, 480
HEADER = ["frame_id", "x", "y", "width", "height", "confidence", "class_id", "gt_id"]


class Obj:
    """세계 좌표에서 움직이는 물체. fn(f) -> (cx, cy, w, h), 등장 구간 start~end."""

    def __init__(self, gid, fn, start=1, end=None, score=0.9):
        self.gid, self.fn, self.start, self.end, self.score = gid, fn, start, end, score


def linear(cx, cy, w, h, vx=0.0, vy=0.0, start=1):
    return lambda f: (cx + vx * (f - start), cy + vy * (f - start), w, h)


def piecewise(cx, cy, w, h, segments, start=1):
    """segments: [(프레임 수, vx, vy), ...] 순서대로 속도를 바꾼다."""
    def fn(f):
        x, y, t = cx, cy, f - start
        for n, vx, vy in segments:
            step = min(t, n)
            x += vx * step
            y += vy * step
            t -= step
            if t <= 0:
                break
        return (x, y, w, h)
    return fn


def clip(box):
    """영상 경계로 자른 박스 (x, y, w, h). 25% 미만만 보이면 None."""
    cx, cy, w, h = box
    x1, y1, x2, y2 = cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2
    vx1, vy1, vx2, vy2 = max(0.0, x1), max(0.0, y1), min(W, x2), min(H, y2)
    vw, vh = vx2 - vx1, vy2 - vy1
    if vw <= 1 or vh <= 1 or vw * vh < 0.25 * w * h:
        return None
    return (vx1, vy1, vw, vh)


def build(name, n_frames, objs, note, cam=None, drop=(), freeze=(), jitter=0.0, seed=0):
    """cam(f) -> (dx, dy): 카메라 위치. drop: Tracker를 호출하지 않는 프레임.
    freeze: 직전 프레임 영상이 그대로 반복되는 프레임 (FFC). jitter: 검출 좌표 흔들림(px, 표준편차)."""
    cam = cam or (lambda f: (0.0, 0.0))
    rnd = random.Random(seed)
    drop, freeze = set(drop), set(freeze)
    rows, frames, last_shown = [], [], {}
    first_seen = {}

    for f in range(1, n_frames + 1):
        # 영상이 정지한 프레임은 직전에 보인 박스를 그대로 반복한다
        if f in freeze:
            shown = dict(last_shown)
        else:
            shown = {}
            dx, dy = cam(f)
            for o in objs:
                end = o.end if o.end is not None else n_frames
                if not (o.start <= f <= end):
                    continue
                cx, cy, w, h = o.fn(f)
                vis = clip((cx - dx, cy - dy, w, h))
                if vis is not None:
                    shown[o.gid] = (vis, o.score)
        last_shown = shown
        if f in drop:
            continue  # 처리가 밀려 Tracker가 이 프레임을 받지 못함
        frames.append(f)
        for gid, ((x, y, w, h), s) in sorted(shown.items()):
            first_seen.setdefault(gid, f)
            rows.append([f, x, y, w, h, s, 0, gid])
            if jitter > 0:
                nx = rnd.gauss(0, jitter)
                ny = rnd.gauss(0, jitter)
                nw = rnd.gauss(0, jitter * 0.5)
                nh = rnd.gauss(0, jitter * 0.5)
                rows[-1][1:5] = [x + nx, y + ny, max(1.5, w + nw), max(1.5, h + nh)]
                rows[-1].append((x, y, w, h))  # 정답 박스 (흔들림 없음)

    assert first_seen, name
    ideal_missed = sum(1 for g, f in first_seen.items() if f != frames[0])

    out = os.path.join(HERE, name)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "detections.csv"), "w", newline="") as fh:
        wr = csv.writer(fh, lineterminator="\n")
        wr.writerow(HEADER + ["gt_x", "gt_y", "gt_width", "gt_height"])
        for r in rows:
            gt = r[8] if len(r) > 8 else tuple(r[1:5])
            wr.writerow([r[0]] + [f"{v:.1f}" for v in r[1:5]] + [f"{r[5]:.2f}", r[6], r[7]] +
                        [f"{v:.1f}" for v in gt])
    with open(os.path.join(out, "frames.csv"), "w", newline="") as fh:
        fh.write("frame_id\n" + "".join(f"{i}\n" for i in frames))
    with open(os.path.join(out, "expected.txt"), "w") as fh:
        fh.write(f"ideal_idsw=0\nideal_false=0\nideal_missed={ideal_missed}\n")
    with open(os.path.join(out, "README.txt"), "w") as fh:
        fh.write(note.strip() + "\n\n")
        fh.write(f"전체 {n_frames}프레임 중 Tracker 호출 {len(frames)}프레임, 물체 {len(objs)}개")
        if drop:
            fh.write(f", 누락 프레임 {len(drop)}개")
        if freeze:
            fh.write(f", 정지 프레임 {len(freeze)}개")
        if jitter:
            fh.write(f", 검출 흔들림 σ={jitter}px")
        fh.write(f"\n이상적인 결과: IDSW 0, 헛출력 0, 누락 {ideal_missed}\n")
    CASES.append(name)


CASES = []
rng = lambda a, b: list(range(a, b + 1))

# =============================================================================
# gr: 짐벌 회전 (카메라 팬). 정지한 물체도 영상에서는 움직인다.
# =============================================================================
build("gr_01", 60,
      [Obj(1, linear(200, 150, 20, 20)), Obj(2, linear(300, 300, 40, 40)), Obj(3, linear(450, 200, 80, 60))],
      "카메라가 오른쪽으로 4px/프레임 일정하게 팬. 정지 물체 3개(20~80px)가 영상에서 왼쪽으로 흐름",
      cam=lambda f: (4.0 * (f - 1) - 100, 0.0))
build("gr_02", 40,
      [Obj(1, linear(500, 200, 30, 30)), Obj(2, linear(700, 320, 30, 30))],
      "빠른 팬 12px/프레임, 작은 물체 30px. 처음 몇 프레임은 속도를 몰라 매칭이 어려움",
      cam=lambda f: (12.0 * (f - 1), 0.0))
build("gr_03", 60,
      [Obj(1, linear(300, 200, 40, 60)), Obj(2, linear(500, 350, 50, 50))],
      "팬 속도가 0에서 20프레임 동안 15px/프레임까지 가속한 뒤 유지",
      cam=lambda f: ((0.375 * (f - 1) ** 2) if f <= 21 else (150.0 + 15.0 * (f - 21)), 0.0))
build("gr_04", 80,
      [Obj(1, linear(320, 240, 40, 40)), Obj(2, linear(150, 120, 30, 50)), Obj(3, linear(500, 380, 60, 40))],
      "좌우 스캔: 카메라가 ±120px 범위를 40프레임 주기로 왕복 (최대 약 19px/프레임)",
      cam=lambda f: (120.0 * math.sin(2 * math.pi * (f - 1) / 40), 0.0))
build("gr_05", 80,
      [Obj(1, linear(320, 240, 40, 40, vx=5, vy=2)),
       Obj(2, linear(200, 120, 50, 30)), Obj(3, linear(520, 380, 40, 60, vx=-2))],
      "짐벌이 표적(ID 1)을 화면 중앙에 두려고 추적 (3프레임 지연). 표적은 거의 정지해 보이고 배경 물체가 반대로 흐름",
      cam=lambda f: (5.0 * max(0, f - 4), 2.0 * max(0, f - 4)))

# =============================================================================
# gj: 짐벌 급출발·급정지·진동
# =============================================================================
build("gj_01", 50,
      [Obj(1, linear(200, 200, 50, 50)), Obj(2, linear(420, 300, 50, 70))],
      "20프레임에 카메라가 한 번에 20px 튐 (중간 크기 물체)",
      cam=lambda f: (20.0 if f >= 20 else 0.0, 0.0))
build("gj_02", 50,
      [Obj(1, linear(200, 200, 24, 24)), Obj(2, linear(400, 250, 24, 24))],
      "20프레임에 카메라가 40px 튐 (작은 물체 24px)",
      cam=lambda f: (40.0 if f >= 20 else 0.0, 0.0))
build("gj_03", 60,
      [Obj(1, linear(320, 200, 40, 40)), Obj(2, linear(450, 330, 50, 50))],
      "15프레임에 정지 상태에서 12px/프레임으로 급출발, 35프레임에 급정지",
      cam=lambda f: (12.0 * min(max(0, f - 15), 20), 0.0))
build("gj_04", 60,
      [Obj(1, linear(200, 200, 40, 40)), Obj(2, linear(420, 300, 30, 30)), Obj(3, linear(320, 100, 60, 40))],
      "짐벌 진동: 카메라가 매 프레임 ±6px 무작위로 떨림",
      cam=lambda f: (random.Random(f).uniform(-6, 6), random.Random(f + 999).uniform(-6, 6)))
build("gj_05", 50,
      [Obj(1, linear(250, 200, 40, 40)), Obj(2, linear(330, 210, 40, 40)), Obj(3, linear(500, 350, 50, 50))],
      "15프레임에 +30px, 30프레임에 -30px 튐. 가까이 있는 두 물체(80px 간격) 포함",
      cam=lambda f: (30.0 if 15 <= f < 30 else 0.0, 0.0))

# =============================================================================
# fd: 프레임 누락 (처리가 밀려 Tracker가 프레임을 건너뜀)
# =============================================================================
build("fd_01", 60,
      [Obj(1, linear(80, 200, 40, 40, vx=6))],
      "3프레임마다 1프레임 누락, 이동 6px/프레임 (누락 뒤에는 12px 이동한 것처럼 보임)",
      drop=[f for f in range(1, 61) if f % 3 == 0])
build("fd_02", 60,
      [Obj(1, linear(80, 240, 40, 40, vx=5))],
      "5프레임 연속 누락 (21~25), 이동 5px/프레임",
      drop=rng(21, 25))
build("fd_03", 70,
      [Obj(1, linear(100, 240, 30, 30, vx=3, vy=1))],
      "10프레임 연속 누락 (21~30), 작은 물체",
      drop=rng(21, 30))
build("fd_04", 60,
      [Obj(1, linear(80, 150, 40, 40, vx=5)), Obj(2, linear(560, 330, 50, 50, vx=-4))],
      "무작위로 30% 프레임 누락, 물체 2개",
      drop=[f for f in range(2, 61) if random.Random(f * 7).random() < 0.3])
build("fd_05", 70,
      [Obj(1, linear(60, 200, 40, 80, vx=6)), Obj(2, linear(500, 212, 40, 80, vx=-6))],
      "두 물체가 교차하는 순간(36~39) 4프레임 누락",
      drop=rng(36, 39))

# =============================================================================
# ff: FFC 셔터 보정. 영상이 몇 프레임 정지했다가 실제 위치로 점프
# =============================================================================
build("ff_01", 50,
      [Obj(1, linear(80, 200, 40, 40, vx=4))],
      "20~24프레임 영상 정지 (5프레임), 이동 4px/프레임",
      freeze=rng(20, 24))
build("ff_02", 60,
      [Obj(1, linear(100, 250, 30, 30, vx=3))],
      "20~29프레임 영상 정지 (10프레임) 후 30px 점프",
      freeze=rng(20, 29))
build("ff_03", 80,
      [Obj(1, linear(60, 150, 40, 40, vx=3)), Obj(2, linear(500, 350, 50, 50, vx=-2, vy=-1))],
      "20프레임마다 3프레임씩 반복 정지 (주기적인 FFC)",
      freeze=[f for f in range(1, 81) if f % 20 in (0, 1, 2) and f > 2])
build("ff_04", 60,
      [Obj(1, linear(300, 200, 40, 40)), Obj(2, linear(450, 330, 50, 50))],
      "카메라 팬 8px/프레임 중 25~31프레임 영상 정지 (7프레임)",
      cam=lambda f: (8.0 * (f - 1) - 100, 0.0), freeze=rng(25, 31))
build("ff_05", 70,
      [Obj(1, linear(60, 200, 40, 80, vx=6)), Obj(2, linear(500, 212, 40, 80, vx=-6))],
      "교차 직전(31~36) 6프레임 영상 정지, 풀리면 두 물체가 겹친 위치로 점프",
      freeze=rng(31, 36))

# =============================================================================
# sm: 아주 작은 원거리 표적 + 검출 흔들림
# =============================================================================
build("sm_01", 50,
      [Obj(1, linear(320, 240, 8, 8))],
      "8x8 정지 표적, 검출 좌표 흔들림 σ=1px",
      jitter=1.0, seed=1)
build("sm_02", 60,
      [Obj(1, linear(200, 200, 6, 6, vx=1))],
      "6x6 표적이 1px/프레임 이동, 흔들림 σ=1px",
      jitter=1.0, seed=2)
build("sm_03", 60,
      [Obj(1, linear(150, 300, 10, 6, vx=2, vy=-0.5))],
      "10x6 표적이 2px/프레임 이동, 흔들림 σ=1.5px",
      jitter=1.5, seed=3)
build("sm_04", 50,
      [Obj(1, linear(300, 240, 8, 8)), Obj(2, linear(330, 240, 8, 8)), Obj(3, linear(315, 265, 8, 8))],
      "8px 표적 3개가 30px 간격으로 모여 있음, 흔들림 σ=1px",
      jitter=1.0, seed=4)
build("sm_05", 50,
      [Obj(1, linear(400, 150, 5, 5))],
      "5x5 정지 표적, 흔들림 σ=1px (크기도 흔들림)",
      jitter=1.0, seed=5)

# =============================================================================
# mv: 급격한 방향 전환 (등속 모델이 틀리는 움직임)
# =============================================================================
build("mv_01", 50,
      [Obj(1, piecewise(100, 150, 40, 40, [(24, 6, 0), (100, 0, 6)]))],
      "6px/프레임으로 오른쪽 이동하다 25프레임에 90도 꺾어 아래로")
build("mv_02", 50,
      [Obj(1, piecewise(150, 240, 30, 30, [(24, 5, 0), (100, -5, 0)]))],
      "5px/프레임으로 가다 25프레임에 180도 반전")
build("mv_03", 60,
      [Obj(1, piecewise(80, 240, 40, 40, [(10, 5, 5), (10, 5, -5), (10, 5, 5), (10, 5, -5), (10, 5, 5), (100, 5, -5)]))],
      "10프레임마다 위아래로 방향을 바꾸는 지그재그 (5px/프레임)")
build("mv_04", 50,
      [Obj(1, piecewise(60, 240, 40, 40, [(19, 2, 0), (100, 12, 0)]))],
      "20프레임에 2px/프레임에서 12px/프레임으로 급가속")
build("mv_05", 80,
      [Obj(1, lambda f: (320 + 90 * math.cos(f / 15.0), 240 + 90 * math.sin(f / 15.0), 36, 36))],
      "반지름 90px 원운동 (6px/프레임, 계속 방향이 바뀜)")

# =============================================================================
# sc: 빠른 크기 변화 (다가오거나 멀어짐, 방향 전환)
# =============================================================================
build("sc_01", 60,
      [Obj(1, lambda f: (320, 240, 20 * 1.03 ** (f - 1), 20 * 1.03 ** (f - 1)))],
      "다가오는 표적: 20px → 약 115px (프레임당 3% 커짐)")
build("sc_02", 60,
      [Obj(1, lambda f: (320, 240, 150 * 0.965 ** (f - 1), 120 * 0.965 ** (f - 1)))],
      "멀어지는 표적: 150px → 약 18px (프레임당 3.5% 작아짐)")
build("sc_03", 40,
      [Obj(1, lambda f: (320, 240, 15 * 1.06 ** (f - 1), 15 * 1.06 ** (f - 1)))],
      "빠르게 다가오는 표적: 15px → 약 145px (프레임당 6% 커짐)")
build("sc_04", 50,
      [Obj(1, lambda f: (300, 240, 30 + 1.2 * (f - 1), 60))],
      "옆으로 도는 표적: 폭 30 → 90px, 높이 60px 고정 (가로세로 비율 변화)")
build("sc_05", 60,
      [Obj(1, lambda f: (80 + 7 * (f - 1), 240, 20 * 1.035 ** (f - 1), 20 * 1.035 ** (f - 1)))],
      "다가오며 가로질러 이동: 7px/프레임, 20px → 약 150px")

# =============================================================================
# ed: 화면 밖으로 나갔다 다시 들어옴 (이상적으로는 같은 ID 유지)
# =============================================================================
build("ed_01", 70,
      [Obj(1, piecewise(520, 240, 40, 40, [(25, 6, 0), (10, 0, 0), (100, -6, 0)]))],
      "오른쪽 끝으로 나갔다가 잠시 뒤 같은 자리로 되돌아옴 (가장자리에서 박스가 잘림)")
build("ed_02", 80,
      [Obj(1, piecewise(120, 150, 40, 60, [(25, -6, 0), (20, 0, 7), (100, 6, 0)]))],
      "왼쪽으로 나가 보이지 않는 동안 아래로 이동한 뒤 다른 높이로 재진입")
build("ed_03", 60,
      [Obj(1, piecewise(320, 400, 50, 60, [(15, 0, 4), (15, 0, -4)]))],
      "아래 가장자리에 반쯤 걸쳤다가(박스가 잘려 작아짐) 다시 올라옴, 완전히 나가지는 않음")
build("ed_04", 60,
      [Obj(1, linear(100, 470, 40, 40, vx=6))],
      "아래 가장자리를 따라 반쯤 잘린 채로 이동 (보이는 높이 약 30px)")
build("ed_05", 90,
      [Obj(1, piecewise(320, 60, 40, 40, [(15, 0, -5), (40, 3, 0), (100, 0, 5)]))],
      "위로 나가서 40프레임 뒤 오른쪽에서 재진입 (track_buffer 30 초과)")

if __name__ == "__main__":
    print(f"생성 완료: {len(CASES)}개 케이스")
