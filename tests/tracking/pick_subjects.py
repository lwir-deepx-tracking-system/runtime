#!/usr/bin/env python3
"""AnyLabeling으로 그린 대상자 박스를 GT ID로 바꿔 subjects.txt를 만든다.

1) 라벨링할 이미지 뽑기 (시퀀스마다 한 장, 8비트로 밝기 조정)
   python3 tests/tracking/pick_subjects.py export --out ~/subject_label [--frame 1]
   → ~/subject_label/seq_001.png ... seq_018.png

2) AnyLabeling으로 이 폴더를 열고, 이미지마다 대상자에 사각형을 그린다 (라벨 이름은 아무거나).
   저장하면 같은 폴더에 seq_001.json ... 이 생긴다. 대상자가 없거나 모르면 그 이미지는 건너뛰면 된다.

3) GT와 맞추기
   python3 tests/tracking/pick_subjects.py match --labels ~/subject_label [--out subjects.txt]

맞추는 방법
  - 그린 박스와 그 프레임 GT 박스의 IoU가 큰 순서로 1:1로 짝짓는다. 겹치지 않으면 중심 거리가 가까운 GT를 고른다.
  - IoU 0.3 미만이거나, 두 번째 후보의 IoU가 첫 번째의 40% 이상이면(애매) ※로 표시한다.
    붙어 선 사람이 많은 장면에서는 손 오차로 옆 사람이 골릴 수 있다 (시험에서 박스당 정답률 약 95%,
    틀린 경우의 약 84%에 ※가 붙음). 그래서 export 폴더에서 돌리면 seq_XXX_check.png 를 함께 만든다:
    초록 = 그린 박스, 빨강 = 고른 GT 박스와 ID. 이 이미지로 최종 확인할 것.
  - 다른 이미지에 그렸어도 된다: 640x510(시각화 머리글 포함), 1280 폭(나란히 보기)도 좌표를 자동으로 맞춘다.
  - 파일 이름이 seq_XXX.json이면 --frame 프레임, seq_XXX/000123.json처럼 프레임 번호 이름이면 그 프레임을 쓴다.
"""
import argparse
import glob
import json
import os
import re
import sys

W, H = 640, 480  # LWIR 원본 크기


def load_gt(path):
    """frame → [(id, x, y, w, h)]"""
    by = {}
    for line in open(path):
        p = line.strip().split(",")
        if len(p) < 6:
            continue
        by.setdefault(int(float(p[0])), []).append((int(float(p[1])), *map(float, p[2:6])))
    return by


def iou(a, b):
    x1, y1 = max(a[0], b[0]), max(a[1], b[1])
    x2, y2 = min(a[0] + a[2], b[0] + b[2]), min(a[1] + a[3], b[1] + b[3])
    inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    union = a[2] * a[3] + b[2] * b[3] - inter
    return inter / union if union > 0 else 0.0


def center_dist(a, b):
    return ((a[0] + a[2] / 2 - b[0] - b[2] / 2) ** 2 + (a[1] + a[3] / 2 - b[1] - b[3] / 2) ** 2) ** 0.5


def boxes_from_json(path):
    """AnyLabeling(LabelMe) JSON의 사각형·다각형을 원본 좌표 (x, y, w, h)로"""
    d = json.load(open(path, encoding="utf-8"))
    iw, ih = d.get("imageWidth", W), d.get("imageHeight", H)
    dy = ih - H if ih in (H + 30,) else 0          # 시각화 이미지의 머리글(30px)
    out = []
    for s in d.get("shapes", []):
        pts = s.get("points") or []
        if len(pts) < 2:
            continue
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        x1, x2, y1, y2 = min(xs), max(xs), min(ys) - dy, max(ys) - dy
        if iw >= 2 * W and x1 >= W:                # 나란히 보기의 오른쪽 패널
            x1, x2 = x1 - W, x2 - W
        if x2 - x1 >= 1 and y2 - y1 >= 1:
            out.append((x1, y1, x2 - x1, y2 - y1))
    return out


def draw_check(img_path, picked):
    """export 이미지가 있으면 그린 박스(초록)와 고른 GT(빨강, ID)를 겹쳐 seq_XXX_check.png 로 저장"""
    if not os.path.exists(img_path):
        return
    import cv2
    img = cv2.imread(img_path, cv2.IMREAD_GRAYSCALE)
    img = cv2.resize(cv2.cvtColor(img, cv2.COLOR_GRAY2BGR), None, fx=2, fy=2, interpolation=cv2.INTER_NEAREST)
    for d, g in picked:
        x, y, w, h = (int(round(2 * v)) for v in d)
        cv2.rectangle(img, (x, y), (x + w, y + h), (0, 200, 0), 1)
        x, y, w, h = (int(round(2 * v)) for v in g[1:])
        cv2.rectangle(img, (x, y), (x + w, y + h), (0, 0, 255), 2)
        label = f"ID {g[0]}"
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.6, 2)
        ty = y - 6 if y - th - 10 > 0 else y + h + th + 6
        cv2.rectangle(img, (x, ty - th - 4), (x + tw + 6, ty + 4), (0, 0, 255), cv2.FILLED)
        cv2.putText(img, label, (x + 3, ty), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.imwrite(img_path.replace(".png", "_check.png"), img)


def cmd_export(a):
    import cv2
    import numpy as np
    os.makedirs(a.out, exist_ok=True)
    seqs = sorted(d for d in os.listdir(a.dataset_root) if re.fullmatch(r"seq_\d+", d))
    for seq in seqs:
        src = os.path.join(a.dataset_root, seq, "images", f"{a.frame:06d}.png")
        if not os.path.exists(src):
            print(f"  {seq}: {src} 없음")
            continue
        img = cv2.imread(src, cv2.IMREAD_UNCHANGED)
        lo, hi = np.percentile(img, [1, 99.5])           # 사람이 잘 보이게 밝기 범위를 맞춤
        out = np.clip((img.astype(np.float32) - lo) * 255.0 / max(hi - lo, 1), 0, 255).astype(np.uint8)
        cv2.imwrite(os.path.join(a.out, f"{seq}.png"), out)
        print(f"  {seq}: 저장 ({a.frame}프레임, 밝기 {lo:.0f}~{hi:.0f})")
    print(f"\nAnyLabeling으로 {a.out} 를 열어 대상자에 사각형을 그리고 저장하세요.")


def cmd_match(a):
    files = sorted(glob.glob(os.path.join(a.labels, "**", "*.json"), recursive=True))
    jobs = {}
    for f in files:
        rel = os.path.relpath(f, a.labels)
        m = re.search(r"(seq_\d+)", rel)
        if not m:
            continue
        stem = os.path.splitext(os.path.basename(f))[0]
        frame = int(stem) if stem.isdigit() else a.frame
        jobs.setdefault(m.group(1), []).append((frame, f))
    if not jobs:
        sys.exit(f"{a.labels} 에서 seq_XXX 이름의 JSON을 찾지 못했습니다")

    lines, report = [], []
    for seq in sorted(jobs):
        gt_path = os.path.join(a.dataset_root, seq, "gt", "gt.txt")
        if not os.path.exists(gt_path):
            report.append(f"  {seq}: GT 없음 → 건너뜀")
            continue
        gt = load_gt(gt_path)
        ids, notes, picked = set(), [], []
        for frame, path in jobs[seq]:
            drawn = boxes_from_json(path)
            cands = gt.get(frame, [])
            if not drawn:
                notes.append(f"{frame}f 박스 없음")
                continue
            if not cands:
                notes.append(f"{frame}f GT 없음")
                continue
            # 모든 (그린 박스, GT) 쌍에 점수: IoU 우선, 겹치지 않으면 중심 거리(키 기준)로
            pairs = []
            for di, d in enumerate(drawn):
                for g in cands:
                    gb = g[1:]
                    pairs.append((-iou(d, gb), center_dist(d, gb) / max(gb[3], 1.0), di, g))
            pairs.sort()
            used_d, used_g = set(), set()
            for neg_iou, dist, di, g in pairs:
                if di in used_d or g[0] in used_g:
                    continue
                used_d.add(di)
                used_g.add(g[0])
                ids.add(g[0])
                d = drawn[di]
                # 같은 그린 박스에 대한 두 번째 후보
                others = sorted((-iou(d, h[1:]), h[0]) for h in cands if h[0] != g[0])
                flag = ""
                if -neg_iou < 0.3:
                    flag = " ※ 겹침 적음"
                if others and -others[0][0] > 0 and -others[0][0] >= 0.4 * -neg_iou:
                    flag += f" ※ 애매(다음 후보 ID {others[0][1]}, IoU {-others[0][0]:.2f})"
                picked.append((d, g))
                notes.append(f"ID {g[0]} (IoU {-neg_iou:.2f}, 거리 {dist:.1f}h, {g[3]:.0f}x{g[4]:.0f}px){flag}")
        if ids:
            lines.append(f"{seq}: {','.join(map(str, sorted(ids)))}")
            report.append(f"  {seq}: " + " | ".join(notes))
            draw_check(os.path.join(a.labels, f"{seq}.png"), picked)

    header = ["# pick_subjects.py match 로 만든 대상자 목록 (AnyLabeling 박스 → 가장 가까운 GT)",
              "# ※ 표시가 있던 시퀀스는 이미지로 확인할 것"]
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(header + lines) + "\n")
    print("\n".join(report))
    print(f"\n저장: {a.out}  ({len(lines)}개 시퀀스)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dataset-root", default="/mnt/lwir_data/datasets")
    ap.add_argument("--frame", type=int, default=1, help="이미지 이름에 프레임 번호가 없을 때 쓰는 프레임")
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("export")
    e.add_argument("--out", required=True)
    m = sub.add_parser("match")
    m.add_argument("--labels", required=True)
    m.add_argument("--out", default="subjects.txt")
    a = ap.parse_args()
    (cmd_export if a.cmd == "export" else cmd_match)(a)


if __name__ == "__main__":
    main()
