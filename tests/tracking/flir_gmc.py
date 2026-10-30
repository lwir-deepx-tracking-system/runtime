#!/usr/bin/env python3
"""영상 프레임으로 카메라 움직임(GMC)을 계산해 bench_mot17 --gmc 가 읽는 파일을 만든다 (FLIR용 CMC).

  python3 tests/tracking/flir_gmc.py datasets/FLIR_MOT --image-root datasets/FLIR_ADAS_1_3/video [--out 폴더]

  - <root>/train/ 아래 시퀀스마다 flir_to_mot.py가 남긴 frames.txt(프레임별 이미지 경로)를 읽어
    <out>/GMC-<시퀀스 이름>.txt 를 만든다 (기본 out = <root>/gmc). 실행: bench_mot17 <root> --det SIM --gmc <out>
  - 형식은 BoT-SORT가 MOT17용으로 공개한 GMC 파일과 같다: 줄 번호(0부터), a, b, tx, c, d, ty (탭 구분).
    n번째 줄 = n프레임 → n+1프레임 변환, 0번째 줄은 항등. 이전 프레임 좌표 (x, y)가 현재 프레임에서
    (a·x + b·y + tx, c·x + d·y + ty) 로 옮겨진다.
  - 계산 방법도 BoT-SORT의 sparseOptFlow와 같다: 축소(--downscale) → 코너 검출(goodFeaturesToTrack) →
    광류 추적(calcOpticalFlowPyrLK) → RANSAC으로 회전·이동·배율 추정(estimateAffinePartial2D).
  - 움직이는 물체 위의 점이 카메라 움직임으로 섞이지 않도록 검출 박스(det.txt) 영역은 코너 검출에서 뺀다
    (--no-mask 로 끔). 실제 시스템에서도 검출 결과는 알 수 있으므로 공정한 조건이다.
  - 16-bit 이미지는 프레임마다 최소~최대를 0~255로 늘려서 쓴다. 차량 보닛처럼 카메라에 붙어 움직이지 않는
    영역이 화면 아래에 있으면 --mask-bottom 0.2 처럼 아래쪽 비율을 빼면 된다.
  - 필요 패키지: opencv-python (없으면 pip install opencv-python-headless)
"""
import argparse
import glob
import math
import os
import sys

try:
    import cv2
    import numpy as np
except ImportError:
    sys.exit("opencv와 numpy가 필요합니다:  pip install opencv-python-headless numpy")


def load_gray(path):
    img = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if img is None:
        raise FileNotFoundError(path)
    if img.ndim == 3:
        img = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    if img.dtype != np.uint8:
        lo, hi = float(img.min()), float(img.max())
        img = ((img.astype(np.float32) - lo) * (255.0 / max(hi - lo, 1.0))).astype(np.uint8)
    return img


def read_dets(path):
    by_frame = {}
    if os.path.exists(path):
        for line in open(path):
            v = line.strip().split(",")
            if len(v) >= 6:
                by_frame.setdefault(int(float(v[0])), []).append(tuple(map(float, v[2:6])))
    return by_frame


class SparseFlowGMC:
    def __init__(self, downscale, mask_bottom):
        self.ds = downscale
        self.mask_bottom = mask_bottom
        self.prev = None
        self.prev_pts = None

    def _small(self, img):
        if self.ds > 1:
            h, w = img.shape
            img = cv2.resize(img, (w // self.ds, h // self.ds))
        return img

    def _mask(self, shape, boxes):
        m = np.full(shape, 255, np.uint8)
        h, w = shape
        if self.mask_bottom > 0:
            m[int(h * (1 - self.mask_bottom)):, :] = 0
        for (x, y, bw, bh) in boxes:
            # 박스보다 조금 넓게 뺀다 (경계의 점도 물체와 같이 움직임)
            x0, y0 = int((x - 0.1 * bw) / self.ds), int((y - 0.1 * bh) / self.ds)
            x1, y1 = int((x + 1.1 * bw) / self.ds), int((y + 1.1 * bh) / self.ds)
            m[max(0, y0):max(0, y1), max(0, x0):max(0, x1)] = 0
        return m

    def apply(self, img, boxes):
        """이전 프레임 → 현재 프레임 변환 (2x3, 원래 해상도 기준). 실패하면 항등, ok=False"""
        cur = self._small(img)
        H = np.eye(2, 3, dtype=np.float64)
        ok, n_in = False, 0
        if self.prev is not None and self.prev_pts is not None and len(self.prev_pts) >= 8:
            nxt, st, _ = cv2.calcOpticalFlowPyrLK(self.prev, cur, self.prev_pts, None)
            good = st.reshape(-1) == 1
            p0, p1 = self.prev_pts[good], nxt[good]
            if len(p0) >= 8:
                M, inl = cv2.estimateAffinePartial2D(p0, p1, method=cv2.RANSAC)
                if M is not None:
                    n_in = int(inl.sum()) if inl is not None else 0
                    if n_in >= 8:
                        H = M.astype(np.float64)
                        H[0, 2] *= self.ds
                        H[1, 2] *= self.ds
                        ok = True
        self.prev = cur
        self.prev_pts = cv2.goodFeaturesToTrack(cur, maxCorners=1000, qualityLevel=0.01, minDistance=1,
                                                blockSize=3, mask=self._mask(cur.shape, boxes))
        return H, ok, n_in


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("root", help="flir_to_mot.py의 --out 폴더")
    p.add_argument("--image-root", required=True, help="frames.txt의 이미지 경로가 시작하는 폴더 (JSON이 있던 video 폴더)")
    p.add_argument("--out", help="GMC 파일 폴더 (기본: <root>/gmc)")
    p.add_argument("--downscale", type=int, default=2)
    p.add_argument("--mask-bottom", type=float, default=0.0)
    p.add_argument("--no-mask", action="store_true", help="검출 박스 영역을 빼지 않는다")
    p.add_argument("--image-dir", help="frames.txt의 폴더 부분을 이 폴더로 바꾼다 (예: thermal_16_bit). 기본은 JSON 경로 그대로(8-bit)")
    a = p.parse_args()

    out = a.out or os.path.join(a.root, "gmc")
    os.makedirs(out, exist_ok=True)
    seqs = sorted(d for d in glob.glob(os.path.join(a.root, "train", "*")) if os.path.isdir(d))
    if not seqs:
        sys.exit(f"시퀀스가 없습니다: {a.root}/train/*")

    for seq in seqs:
        name = os.path.basename(seq)
        flist = os.path.join(seq, "frames.txt")
        if not os.path.exists(flist):
            print(f"  {name}: frames.txt가 없어 건너뜀 (flir_to_mot.py로 다시 변환하세요)")
            continue
        files = [l.strip() for l in open(flist) if l.strip()]
        if a.image_dir:
            # 폴더만 바꾸고, 확장자가 다르면 (.jpeg → .tiff 등) 같은 이름의 다른 확장자를 찾는다
            moved = []
            for f in files:
                cand = os.path.join(a.image_dir, os.path.basename(f))
                if not os.path.exists(os.path.join(a.image_root, cand)):
                    stem = os.path.splitext(os.path.basename(f))[0]
                    hits = glob.glob(os.path.join(a.image_root, a.image_dir, stem + ".*"))
                    if hits:
                        cand = os.path.relpath(hits[0], a.image_root)
                moved.append(cand)
            files = moved
        dets = {} if a.no_mask else read_dets(os.path.join(seq, "det", "det.txt"))
        gmc = SparseFlowGMC(a.downscale, a.mask_bottom)
        lines, fails, shifts = [], 0, []
        for i, f in enumerate(files):
            img = load_gray(os.path.join(a.image_root, f))
            H, ok, _ = gmc.apply(img, dets.get(i + 1, []))
            if i > 0 and not ok:
                fails += 1
            shifts.append(math.hypot(H[0, 2], H[1, 2]))
            lines.append(f"{i}\t" + "\t".join(f"{v:.6f}" for v in (H[0, 0], H[0, 1], H[0, 2], H[1, 0], H[1, 1], H[1, 2])))
            if (i + 1) % 500 == 0:
                print(f"  {name}: {i + 1}/{len(files)}", flush=True)
        path = os.path.join(out, f"GMC-{name}.txt")
        with open(path, "w") as fh:
            fh.write("\n".join(lines) + "\n")
        s = sorted(shifts[1:]) or [0.0]
        print(f"  {name}: {len(files)}프레임 → {path}\n"
              f"    프레임당 이동량 평균 {sum(s) / len(s):.2f}px, 중앙값 {s[len(s) // 2]:.2f}px, 최대 {s[-1]:.1f}px, "
              f"추정 실패(항등 처리) {fails}프레임")
    print(f"\n실행: bench_mot17 {a.root} --det <SIM|YOLO> --gmc {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
