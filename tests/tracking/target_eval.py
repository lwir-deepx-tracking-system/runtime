#!/usr/bin/env python3
"""이미 나온 트래킹 결과에서 대상 하나를 골라, 그 대상이 얼마나 잘 추적됐는지 지표를 뽑는다 (정답 없이).

  python3 tests/tracking/target_eval.py <tracks 파일> --frame 1 --id 1 [--det <detections 파일>] [--fps 23]
  python3 tests/tracking/target_eval.py <tracks 파일> --frame 1 --box 403,228,124,250   (ID 대신 박스로 지정)
  여러 결과를 비교: tracks 파일을 여러 개 주면 같은 대상(같은 시작 박스)을 각각 따라가 표로 비교한다

tracks 파일: Benchmark 결과 tracks/<시퀀스>.txt (frame,id,x,y,w,h,score,...) 또는 같은 열의 csv
detections : (선택) Benchmark 결과 detections/<시퀀스>.txt. 주면 끊긴 구간마다 "검출은 있었는데 tracker가 놓쳤는지"
             "검출 자체가 없었는지"를 구분한다

대상 따라가기 (정답이 없으므로 위치 연속성으로 추정)
  - 고른 ID가 계속 나오는 동안은 그 ID를 대상으로 본다
  - 그 ID가 사라지면, 마지막 위치(속도로 예측) 근처에 처음 나타나는 Track을 같은 대상으로 본다
      같은 ID가 다시 나타나면 → 끊김 후 복구 (ID 유지)
      다른 ID가 나타나면     → ID 전환(IDSW)
      --max-gap 초 안에 아무것도 없으면 → 대상 놓침 (따라가기 종료)
  - 같은 ID인데 한 프레임에 박스가 크게 튀면 → 다른 물체로 옮겨 붙었을 수 있음 (드리프트 의심)
  출력되는 프레임 번호를 visualization 이미지로 확인하면 추정이 맞는지 바로 알 수 있다.

지표
  ID 전환(IDSW) 횟수, 끊김(같은 ID로 복구) 횟수, 대상 추적 시간과 비율, 가장 오래 유지된 ID 시간,
  놓친 프레임(검출 있음 = tracker가 놓침 / 검출 없음 = 검출기가 놓침), 드리프트 의심 지점
"""
import argparse
import collections
import os
import sys


def read_boxes(path, track=True, person_only=True, min_score=0.1):
    """frame → [(id 또는 score, (x, y, w, h))]"""
    by_frame = collections.defaultdict(list)
    for line in open(path):
        p = [s.strip() for s in line.split(",")]
        if len(p) < 6 or not p[0].isdigit():
            continue
        if track:  # frame,id,x,y,w,h,score
            by_frame[int(p[0])].append((int(float(p[1])), tuple(map(float, p[2:6]))))
        else:      # frame,class,score,x,y,w,h  (csv면 class_name 열이 끼어 있을 수 있음)
            if len(p) >= 8:
                try:
                    float(p[2])
                except ValueError:
                    p = p[:2] + p[3:]
            if person_only and int(float(p[1])) != 0:
                continue
            if float(p[2]) <= min_score:
                continue
            by_frame[int(p[0])].append((float(p[2]), tuple(map(float, p[3:7]))))
    return by_frame


def iou(a, b):
    ax2, ay2, bx2, by2 = a[0] + a[2], a[1] + a[3], b[0] + b[2], b[1] + b[3]
    iw = max(0.0, min(ax2, bx2) - max(a[0], b[0]))
    ih = max(0.0, min(ay2, by2) - max(a[1], b[1]))
    inter = iw * ih
    u = a[2] * a[3] + b[2] * b[3] - inter
    return inter / u if u > 0 else 0.0


def center(b):
    return b[0] + b[2] / 2, b[1] + b[3] / 2


def near(pred, cand, tol):
    """예측 박스와 후보가 같은 대상으로 볼 만큼 가까운지 (위치 + 크기)"""
    (px, py), (cx, cy) = center(pred), center(cand)
    d = ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5
    ratio = cand[3] / pred[3] if pred[3] > 0 else 0
    return (iou(pred, cand) >= 0.3 or d <= tol * max(pred[3], 1.0)) and 0.5 <= ratio <= 2.0


def follow(tracks, start_frame, start_id, max_gap, tol, last_frame):
    """대상을 따라가며 프레임별 (frame, id 또는 None, box 또는 예측) 기록과 사건 목록을 만든다"""
    cur = start_id
    box = dict(tracks[start_frame]).get(cur)
    vel = (0.0, 0.0)
    timeline, events = [], []
    gap_start = None
    f = start_frame
    while f <= last_frame:
        here = dict(tracks.get(f, []))
        if cur in here:
            nb = here[cur]
            if gap_start is None and box is not None:
                (ox, oy), (nx, ny) = center(box), center(nb)
                jump = ((nx - ox) ** 2 + (ny - oy) ** 2) ** 0.5
                if jump > 0.8 * max(box[3], nb[3]) or not (0.6 <= nb[3] / max(box[3], 1e-6) <= 1.67):
                    events.append((f, "drift", cur, cur, f"이동 {jump:.0f}px, 높이 {box[3]:.0f}→{nb[3]:.0f}"))
                vel = (0.7 * vel[0] + 0.3 * (nx - ox), 0.7 * vel[1] + 0.3 * (ny - oy))
            if gap_start is not None:
                events.append((f, "recover", cur, cur, f"{f - gap_start}프레임 끊김 뒤 같은 ID로 복구"))
                gap_start = None
            box = nb
            timeline.append((f, cur, nb))
            f += 1
            continue
        # 대상 ID가 없음: 예측 위치 근처의 다른 Track을 찾는다
        if gap_start is None:
            gap_start = f
        k = f - gap_start + 1
        pred = (box[0] + vel[0] * k, box[1] + vel[1] * k, box[2], box[3])
        cands = [(i, b) for i, b in here.items() if near(pred, b, tol * (1 + 0.1 * k))]
        if cands:
            i, b = max(cands, key=lambda c: iou(pred, c[1]))
            events.append((f, "idsw", cur, i, f"{f - gap_start}프레임 끊김 뒤 ID {cur} → {i}" if f > gap_start
                           else f"ID {cur} → {i} (끊김 없이 바로)"))
            cur, box, gap_start = i, b, None
            timeline.append((f, cur, b))
            f += 1
            continue
        timeline.append((f, None, pred))
        if f - gap_start + 1 > max_gap:
            events.append((gap_start, "lost", cur, None, f"{max_gap}프레임 동안 근처에 Track 없음 → 대상 놓침"))
            # 놓친 뒤 구간은 기록하지 않는다
            timeline = timeline[: len(timeline) - (f - gap_start + 1)]
            break
        f += 1
    # 끝에 남은 끊김(시퀀스 종료)
    if gap_start is not None and timeline and timeline[-1][1] is None:
        while timeline and timeline[-1][1] is None:
            timeline.pop()
        events.append((gap_start, "end_gap", cur, None, "시퀀스 끝까지 다시 나타나지 않음"))
    return timeline, events


def evaluate(path, a, start_box=None):
    tracks = read_boxes(path, track=True)
    if not tracks:
        sys.exit(f"{path}: Track이 없습니다")
    last = max(tracks)
    sf = a.frame
    if start_box is not None:
        cands = [(i, b) for i, b in tracks.get(sf, [])]
        if not cands:
            return None
        sid, _ = max(cands, key=lambda c: iou(start_box, c[1]))
        if iou(start_box, dict(cands)[sid]) < 0.3:
            return None
    else:
        sid = a.id
        if sid not in dict(tracks.get(sf, [])):
            ids = sorted(i for i, _ in tracks.get(sf, []))
            sys.exit(f"{path}: {sf}프레임에 ID {sid}가 없습니다. 이 프레임의 ID: {ids}")
    max_gap = max(1, int(round(a.max_gap * a.fps)))
    timeline, events = follow(tracks, sf, sid, max_gap, a.tol, last)
    dets = read_boxes(a.det, track=False) if a.det else None

    frames = len(timeline)
    tracked = [t for t in timeline if t[1] is not None]
    gaps = [t for t in timeline if t[1] is None]
    # 끊긴 프레임 원인: 예측 위치 근처에 검출이 있었는지
    det_miss = trk_miss = 0
    if dets is not None:
        for f, _, pred in gaps:
            if any(near(pred, b, a.tol) for _, b in dets.get(f, [])):
                trk_miss += 1
            else:
                det_miss += 1
    # ID별 유지 구간
    segs, prev = [], None
    for f, i, _ in timeline:
        if i is None:
            continue
        if segs and segs[-1][0] == i and f - segs[-1][2] <= max_gap + 1:
            segs[-1][2] = f
            segs[-1][3] += 1
        else:
            segs.append([i, f, f, 1])
    longest = max(segs, key=lambda s: s[3]) if segs else None
    end_frame = timeline[-1][0] if timeline else sf
    return {
        "name": path, "start_id": sid, "frames": frames, "start": sf, "end": end_frame,
        "tracked": len(tracked), "gaps": len(gaps), "det_miss": det_miss, "trk_miss": trk_miss,
        "idsw": sum(1 for e in events if e[1] == "idsw"),
        "recover": sum(1 for e in events if e[1] == "recover"),
        "drift": sum(1 for e in events if e[1] == "drift"),
        "lost": any(e[1] == "lost" for e in events),
        "ids": [s[0] for s in segs], "segs": segs, "longest": longest, "events": events,
        "start_box": dict(tracks[sf])[sid],
    }


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("tracks", nargs="+")
    p.add_argument("--frame", type=int, default=1, help="대상을 고를 프레임")
    p.add_argument("--id", type=int, help="그 프레임에서 대상의 Track ID (첫 번째 tracks 파일 기준)")
    p.add_argument("--box", help="ID 대신 x,y,w,h 로 지정")
    p.add_argument("--det", help="detections 파일 (선택)")
    p.add_argument("--fps", type=float, default=30.0, help="실제 영상 fps (시간 환산용)")
    p.add_argument("--max-gap", type=float, default=2.0, help="이 시간(초) 넘게 안 보이면 대상 놓침")
    p.add_argument("--tol", type=float, default=1.0, help="같은 대상으로 볼 거리 (박스 높이 배수)")
    p.add_argument("--events", type=int, default=15, help="출력할 사건 수 (파일마다)")
    a = p.parse_args()
    if a.id is None and a.box is None:
        p.error("--id 또는 --box 가 필요합니다")

    start_box = tuple(map(float, a.box.split(","))) if a.box else None
    results = []
    for i, path in enumerate(a.tracks):
        r = evaluate(path, a, start_box)
        if r is None:
            print(f"{path}: {a.frame}프레임에 지정한 박스와 겹치는 Track이 없습니다")
            continue
        if start_box is None:
            start_box = r["start_box"]  # 다음 파일들은 같은 박스로 같은 대상을 찾는다
        results.append(r)

    s = lambda n: f"{n / a.fps:.1f}s"
    for r in results:
        print(f"\n== {r['name']}")
        print(f"  대상: {r['start']}프레임의 ID {r['start_id']} (박스 {', '.join(f'{v:.0f}' for v in r['start_box'])})")
        print(f"  따라간 구간: {r['start']}~{r['end']}프레임 ({r['frames']}프레임, {s(r['frames'])})"
              + ("  ※ 중간에 대상 놓침" if r["lost"] else ""))
        print(f"  추적된 프레임: {r['tracked']} ({100 * r['tracked'] / max(r['frames'], 1):.1f}%)"
              f", 끊긴 프레임: {r['gaps']}"
              + (f" (검출은 있었는데 tracker가 놓침 {r['trk_miss']}, 검출 자체가 없음 {r['det_miss']})" if a.det else ""))
        print(f"  ID 전환(IDSW): {r['idsw']}회, 끊김 후 같은 ID로 복구: {r['recover']}회, 드리프트 의심: {r['drift']}회")
        if r["longest"]:
            i, f0, f1, n = r["longest"]
            print(f"  가장 오래 유지된 ID: {i} ({f0}~{f1}프레임, {n}프레임 = {s(n)})")
        segs = ", ".join(f"{sg[0]}({sg[3]})" for sg in r["segs"][:12]) + (" ..." if len(r["segs"]) > 12 else "")
        print(f"  거쳐 간 ID(유지 프레임): {segs}")
        ev = [e for e in r["events"] if e[1] != "recover"] + [e for e in r["events"] if e[1] == "recover"]
        ev.sort()
        if ev:
            print("  사건 (visualization 이미지에서 확인할 프레임):")
            kinds = {"idsw": "ID 전환", "recover": "복구", "drift": "드리프트?", "lost": "놓침", "end_gap": "끝"}
            for f, k, _, _, msg in ev[: a.events]:
                print(f"    {f:6d}  {kinds[k]:<6} {msg}")
            if len(ev) > a.events:
                print(f"    ... 외 {len(ev) - a.events}건 (--events 로 더 보기)")

    if len(results) > 1:
        print("\n== 비교")
        print(f"  {'결과':<44}{'추적 비율':>9}{'IDSW':>6}{'복구':>6}{'드리프트?':>9}{'최장 유지':>10}")
        for r in results:
            n = r["longest"][3] if r["longest"] else 0
            name = r["name"] if len(r["name"]) <= 42 else "…" + r["name"][-41:]
            print(f"  {name:<44}{100 * r['tracked'] / max(r['frames'], 1):8.1f}%{r['idsw']:6d}{r['recover']:6d}"
                  f"{r['drift']:9d}{s(n):>10}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
