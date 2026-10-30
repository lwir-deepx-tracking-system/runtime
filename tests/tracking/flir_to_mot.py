#!/usr/bin/env python3
"""FLIR ADAS (COCO 형식 thermal_annotations.json)를 bench_mot17이 읽는 MOT 폴더 구조로 바꾼다.

  python3 tests/tracking/flir_to_mot.py <thermal_annotations.json> [--out datasets/FLIR_MOT] [--classes person]
                                        [--fps 30] [--det sim|csv] [--det-csv 파일] [--split-at 프레임 ...]

만드는 구조 (bench_mot17 datasets/FLIR_MOT --det SIM 으로 바로 실행)
  <out>/train/FLIR-video-SIM/seqinfo.ini
  <out>/train/FLIR-video-SIM/gt/gt.txt     frame, id, x, y, w, h, consider, class, visibility
  <out>/train/FLIR-video-SIM/det/det.txt   frame, -1, x, y, w, h, score
  <out>/train/FLIR-video-SIM/frames.txt    프레임별 이미지 경로 (JSON의 file_name, CMC용 flir_gmc.py가 읽음)

주의
  - FLIR ADAS v1.3 라벨에는 프레임 사이를 잇는 객체 ID(track ID)가 없다. gt.txt의 id는 프레임 안 순번이라서
    bench 출력의 IDSW·IDF1·MOTA는 의미가 없다. TP·헛출력·누락·재현율·정밀도만 본다 (flir_summary.py).
  - 프레임 번호는 images를 file_name 순으로 정렬해 1부터 매긴다.
  - --classes에 없는 클래스의 박스는 정답에서 뺀다. iscrowd=1 박스는 방해 클래스(8)로 넣어,
    그 위에 나온 출력은 헛출력으로 세지 않는다 (MOT17과 같은 규칙).

검출 (--det)
  sim : 정답 박스로 가짜 검출을 만든다 (검출기 없이 파이프라인 확인용). 결과는 아래 잡음 설정에 따라 달라진다.
        --drop 누락 확률, --jitter 위치·크기 흔들림(박스 크기 대비 표준편차), --low 낮은 점수(가려짐 흉내) 비율,
        --fp 프레임당 헛검출 평균 개수, --seed 난수 시드
  csv : 팀 Benchmark detections 파일(frame_id,class_id,confidence,x,y,width,height). --det-class로 쓸 class_id 지정
        (생략하면 모든 클래스). 폴더 이름은 FLIR-video-YOLO
"""
import argparse
import json
import math
import os
import random
import re
import sys
from collections import Counter, defaultdict

# categories가 없을 때 쓰는 FLIR ADAS v1.3 번호 (COCO 번호를 따른다)
DEFAULT_NAMES = {1: "person", 2: "bicycle", 3: "car", 17: "dog", 18: "dog"}


def iou(a, b):
    ax2, ay2, bx2, by2 = a[0] + a[2], a[1] + a[3], b[0] + b[2], b[1] + b[3]
    iw = max(0.0, min(ax2, bx2) - max(a[0], b[0]))
    ih = max(0.0, min(ay2, by2) - max(a[1], b[1]))
    inter = iw * ih
    union = a[2] * a[3] + b[2] * b[3] - inter
    return inter / union if union > 0 else 0.0


def frame_key(img):
    nums = re.findall(r"\d+", os.path.basename(img["file_name"]))
    return (int(nums[-1]) if nums else img["id"], img["id"])


def find_cuts(frames, n_frames):
    """장면이 바뀌었을 수 있는 프레임: 이웃 프레임의 정답 박스 대부분이 가까운 짝을 찾지 못하는 곳.
    카메라가 빠르게 움직이면 IoU가 0이 될 수 있으므로 IoU 대신 중심 거리(박스 크기 기준)로 본다."""
    def near(c, p):
        cx, cy = c[0] + c[2] / 2, c[1] + c[3] / 2
        px, py = p[0] + p[2] / 2, p[1] + p[3] / 2
        return math.hypot(cx - px, cy - py) <= 1.0 * max(c[2], c[3], p[2], p[3])
    cuts = []
    for f in range(2, n_frames + 1):
        prev, cur = frames.get(f - 1, []), frames.get(f, [])
        if len(prev) < 3 or len(cur) < 3:
            continue
        linked = sum(1 for c in cur if any(near(c["bbox"], p["bbox"]) for p in prev))
        if linked / len(cur) < 0.3:
            cuts.append(f)
    return cuts


def sim_detections(frames, n_frames, w_img, h_img, a, rng):
    rows = []
    for f in range(1, n_frames + 1):
        for g in frames.get(f, []):
            if g["crowd"] or rng.random() < a.drop:
                continue
            x, y, w, h = g["bbox"]
            jx, jy = rng.gauss(0, a.jitter * w), rng.gauss(0, a.jitter * h)
            sw, sh = 1 + rng.gauss(0, a.jitter), 1 + rng.gauss(0, a.jitter)
            w2, h2 = max(2.0, w * sw), max(2.0, h * sh)
            x2, y2 = x + jx - (w2 - w) / 2, y + jy - (h2 - h) / 2
            # 작은 물체일수록, 그리고 --low 비율만큼 낮은 점수 (ByteTrack 2차 매칭 대상)
            small = min(1.0, math.sqrt(w * h) / 40.0)
            if rng.random() < a.low:
                score = rng.uniform(0.15, 0.5)
            else:
                score = min(0.99, max(0.5, rng.gauss(0.6 + 0.3 * small, 0.08)))
            rows.append((f, x2, y2, w2, h2, score))
        # 헛검출: 포아송 근사로 개수, 위치·크기는 무작위, 점수는 낮은 편
        k = sum(1 for _ in range(10) if rng.random() < a.fp / 10.0)
        for _ in range(k):
            w, h = rng.uniform(10, 40), rng.uniform(20, 80)
            rows.append((f, rng.uniform(0, w_img - w), rng.uniform(0, h_img - h), w, h, rng.uniform(0.15, 0.6)))
    return rows


def csv_detections(path, frame_of_index, det_class):
    rows, skipped = [], 0
    with open(path) as fh:
        for line in fh:
            parts = line.strip().split(",")
            if len(parts) < 7 or not parts[0].lstrip("-").isdigit():
                continue  # 머리글
            fid, cls, conf, x, y, w, h = parts[:7]
            if det_class is not None and int(cls) not in det_class:
                continue
            f = frame_of_index(int(fid))
            if f is None:
                skipped += 1
                continue
            rows.append((f, float(x), float(y), float(w), float(h), float(conf)))
    if skipped:
        print(f"  ! 프레임 범위를 벗어난 검출 {skipped}개는 뺐습니다 (--det-frame-offset 확인)")
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("json")
    p.add_argument("--out", default="datasets/FLIR_MOT")
    p.add_argument("--name", default="FLIR-video")
    p.add_argument("--classes", nargs="+", default=["person"], help="평가할 클래스 이름 (all: 전부)")
    p.add_argument("--fps", type=int, default=30, help="영상 fps (track_buffer 환산에 쓰임)")
    p.add_argument("--split-at", type=int, nargs="*", default=[], help="이 프레임에서 새 시퀀스를 시작한다")
    p.add_argument("--det", choices=["sim", "csv"], default="sim")
    p.add_argument("--det-csv")
    p.add_argument("--det-class", type=int, nargs="*", help="csv 검출 중 쓸 class_id (생략: 전부)")
    p.add_argument("--det-frame-offset", type=int, default=0,
                   help="csv의 frame_id + 이 값 = 이 스크립트의 프레임 번호(1부터)")
    p.add_argument("--drop", type=float, default=0.10)
    p.add_argument("--jitter", type=float, default=0.03)
    p.add_argument("--low", type=float, default=0.10)
    p.add_argument("--fp", type=float, default=0.3)
    p.add_argument("--seed", type=int, default=0)
    a = p.parse_args()
    if a.det == "csv" and not a.det_csv:
        p.error("--det csv 에는 --det-csv 파일이 필요합니다")

    d = json.load(open(a.json))
    names = {c["id"]: c["name"] for c in d.get("categories", [])} or DEFAULT_NAMES
    if not d.get("categories"):
        print("  ! categories가 없어 FLIR 기본 번호로 클래스 이름을 정합니다:", DEFAULT_NAMES)
    want = None if a.classes == ["all"] else set(a.classes)

    imgs = sorted(d["images"], key=frame_key)
    frame_of_img = {img["id"]: i + 1 for i, img in enumerate(imgs)}
    n_frames = len(imgs)
    w_img = max(i.get("width", 640) for i in imgs)
    h_img = max(i.get("height", 512) for i in imgs)

    frames = defaultdict(list)
    cls_count, kept = Counter(), Counter()
    for ann in d["annotations"]:
        name = names.get(ann["category_id"], f"cls{ann['category_id']}")
        cls_count[name] += 1
        if want is not None and name not in want:
            continue
        x, y, w, h = ann["bbox"]
        if w <= 0 or h <= 0:
            continue
        kept[name] += 1
        frames[frame_of_img[ann["image_id"]]].append(
            {"bbox": (float(x), float(y), float(w), float(h)), "crowd": ann.get("iscrowd", 0) == 1})

    print(f"프레임 {n_frames}개 ({imgs[0]['file_name']} ~ {imgs[-1]['file_name']}), 영상 크기 {w_img}x{h_img}")
    print("클래스별 박스 수:", dict(cls_count.most_common()))
    print("평가에 쓰는 박스:", dict(kept), f"/ 박스가 있는 프레임 {sum(1 for f in frames if frames[f])}개")

    outside = sum(1 for f in frames.values() for g in f
                  if g["bbox"][0] + g["bbox"][2] > w_img + 2 or g["bbox"][1] + g["bbox"][3] > h_img + 2)
    if outside:
        print(f"  ! 영상 크기({w_img}x{h_img})를 벗어난 정답 박스 {outside}개. images의 width/height가 맞는지 확인하세요"
              " (틀리면 lost_output_frames의 화면 밖 판정이 잘못됩니다)")

    cuts = find_cuts(frames, n_frames)
    if cuts:
        print(f"  ! 정답 박스가 거의 이어지지 않는 프레임 {len(cuts)}곳 (장면 전환일 수 있음): {cuts[:20]}"
              + (" ..." if len(cuts) > 20 else ""))
        print("    영상으로 확인 후 장면 전환이면 --split-at 으로 나누세요 (Tracker가 시퀀스마다 초기화됨)")

    rng = random.Random(a.seed)
    if a.det == "sim":
        dets = sim_detections(frames, n_frames, w_img, h_img, a, rng)
        det_tag = "SIM"
    else:
        def frame_of_index(fid):
            f = fid + a.det_frame_offset
            return f if 1 <= f <= n_frames else None
        dets = csv_detections(a.det_csv, frame_of_index, set(a.det_class) if a.det_class else None)
        det_tag = "YOLO"

    bounds = sorted({1, *[s for s in a.split_at if 1 < s <= n_frames]}) + [n_frames + 1]
    det_by_frame = defaultdict(list)
    for r in dets:
        det_by_frame[r[0]].append(r)

    for si, (start, end) in enumerate(zip(bounds, bounds[1:])):
        name = a.name if len(bounds) == 2 else f"{a.name}{si + 1:02d}"
        seq = os.path.join(a.out, "train", f"{name}-{det_tag}")
        os.makedirs(os.path.join(seq, "gt"), exist_ok=True)
        os.makedirs(os.path.join(seq, "det"), exist_ok=True)
        length = end - start
        with open(os.path.join(seq, "seqinfo.ini"), "w") as fh:
            fh.write(f"[Sequence]\nname={name}-{det_tag}\nimDir=thermal_8_bit\nframeRate={a.fps}\n"
                     f"seqLength={length}\nimWidth={w_img}\nimHeight={h_img}\nimExt=.jpeg\n")
        n_gt = n_det = 0
        with open(os.path.join(seq, "gt", "gt.txt"), "w") as fh:
            for f in range(start, end):
                # track ID가 없으므로 프레임 안에서의 순번(1, 2, ...)을 id로 쓴다. 박스마다 다른 번호를 주면
                # bench의 IDF1 계산(정답 id 수 × 정답 id 수 행렬의 헝가리안)이 박스 수만큼 커져 끝나지 않는다
                for uid, g in enumerate(frames.get(f, []), start=1):
                    x, y, w, h = g["bbox"]
                    cls = 8 if g["crowd"] else 1
                    fh.write(f"{f - start + 1},{uid},{x:.2f},{y:.2f},{w:.2f},{h:.2f},1,{cls},1\n")
                    n_gt += 0 if g["crowd"] else 1
        with open(os.path.join(seq, "det", "det.txt"), "w") as fh:
            for f in range(start, end):
                for (_, x, y, w, h, s) in det_by_frame.get(f, []):
                    fh.write(f"{f - start + 1},-1,{x:.2f},{y:.2f},{w:.2f},{h:.2f},{s:.3f}\n")
                    n_det += 1
        # 프레임별 이미지 경로 (flir_gmc.py가 카메라 움직임을 계산할 때 사용)
        with open(os.path.join(seq, "frames.txt"), "w") as fh:
            for f in range(start, end):
                fh.write(imgs[f - 1]["file_name"] + "\n")
        print(f"  {seq}: {length}프레임, 정답 {n_gt}개, 검출 {n_det}개")

    print(f"\n실행: bench_mot17 {a.out} --det {det_tag}   (요약: python3 tests/tracking/flir_summary.py {a.out} --det {det_tag})")


if __name__ == "__main__":
    sys.exit(main())
