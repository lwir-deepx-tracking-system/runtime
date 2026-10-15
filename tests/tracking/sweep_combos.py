#!/usr/bin/env python3
"""정해 둔 12개 조합 × CMC 끔/켬 = 24개 설정을 한 번에 돌려 표로 비교한다 (FLIR, MOT17 공용).

  # FLIR (track ID 없는 정답): TP·헛출력·누락·재현율·정밀도
  python3 tests/tracking/sweep_combos.py datasets/FLIR_MOT --det SIM --gmc datasets/FLIR_MOT/gmc
  # MOT17: --ids 를 붙이면 IDSW·IDF1·MOTA도 표에 넣는다
  python3 tests/tracking/sweep_combos.py datasets/MOT17 --det FRCNN --gmc datasets/MOT17_gmc --ids [시퀀스 필터...]

조합 (C1 = fuse_score + track_buffer 50 + lost_output_frames 1)
  기본값, buffer 50, fuse_score, fuse + buffer 50, L1, C1,
  biou 0.5, biou 0.5 + snap, C1 + biou 0.5, nsa, C1 + nsa, C1 + biou 0.5 + nsa
  --gmc 를 주면 위 12개를 CMC 끔/켬으로 두 번 돌린다 (24개). 주지 않으면 CMC 끔 12개만.

결과: <out>/summary.md, <out>/raw.csv, <out>/configs/, <out>/logs/  (기본 out = results/combos_<det>)
"""
import argparse
import csv
import glob
import os
import re
import subprocess
import sys

BASE = {"track_threshold": 0.5, "match_threshold": 0.8, "track_buffer": 30}
C1 = {"fuse_score": True, "track_buffer": 50, "lost_output_frames": 1}
BIOU = {"buffer_scale": 0.5}
NSA = {"nsa_kalman": True}

COMBOS = [
    ("기본값", {}),
    ("buffer 50", {"track_buffer": 50}),
    ("fuse_score", {"fuse_score": True}),
    ("fuse + buffer 50", {"fuse_score": True, "track_buffer": 50}),
    ("L1", {"lost_output_frames": 1}),
    ("C1", dict(C1)),
    ("biou 0.5", dict(BIOU)),
    ("biou 0.5 + snap", dict(BIOU, buffer_snap=True)),
    ("C1 + biou 0.5", dict(C1, **BIOU)),
    ("nsa", dict(NSA)),
    ("C1 + nsa", dict(C1, **NSA)),
    ("C1 + biou 0.5 + nsa", dict(C1, **BIOU, **NSA)),
]

ROW = re.compile(r"^\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)%\s+(-?[\d.]+)%\s+[\d.]+\s*$")


def find_bench():
    hits = [p for p in glob.glob("build/**/bench_mot17", recursive=True) if os.access(p, os.X_OK)]
    if not hits:
        sys.exit("bench_mot17을 찾지 못했습니다. 빌드하거나 --bench로 경로를 지정하세요")
    return max(hits, key=os.path.getmtime)


def write_yaml(path, changes):
    cfg = dict(BASE, **changes)
    with open(path, "w") as f:
        for k, v in cfg.items():
            f.write(f"{k}: {str(v).lower() if isinstance(v, bool) else v}\n")


def run(cmd, log):
    res = subprocess.run(cmd, capture_output=True, text=True)
    with open(log, "w") as f:
        f.write(res.stdout + res.stderr)
    if res.returncode != 0:
        sys.exit(f"bench 실패: {' '.join(cmd)}\n{res.stderr or res.stdout}")
    for line in res.stdout.splitlines():
        m = ROW.match(line)
        if m and m.group(1) == "합계":
            gt, pred, idsw, fp, fn = map(int, m.groups()[1:6])
            return {"gt": gt, "pred": pred, "tp": gt - fn, "fp": fp, "fn": fn, "idsw": idsw,
                    "idf1": float(m.group(7)), "mota": float(m.group(8))}
    sys.exit(f"bench 출력에서 합계 줄을 찾지 못했습니다: {log}")


def pct(a, b):
    return 100.0 * a / b if b else 0.0


def d(v, ref, fmt="{:,}"):
    diff = v - ref
    if isinstance(v, float):
        return f"{v:.1f}% ({diff:+.1f})"
    return f"{fmt.format(v)} ({'+' if diff > 0 else ''}{diff:,})"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("root")
    p.add_argument("filters", nargs="*", help="시퀀스 이름 필터 (bench_mot17과 같음)")
    p.add_argument("--det", default="SIM")
    p.add_argument("--gmc", help="GMC 파일 폴더. 주면 CMC 켬 12개를 추가로 돌린다")
    p.add_argument("--ids", action="store_true", help="IDSW·IDF1·MOTA도 표에 넣는다 (정답에 track ID가 있을 때만)")
    p.add_argument("--out")
    p.add_argument("--bench")
    a = p.parse_args()

    bench = a.bench or find_bench()
    out = a.out or f"results/combos_{a.det}"
    os.makedirs(os.path.join(out, "configs"), exist_ok=True)
    os.makedirs(os.path.join(out, "logs"), exist_ok=True)
    modes = [("CMC X", None)] + ([("CMC O", a.gmc)] if a.gmc else [])
    print(f"bench: {bench}\n데이터: {a.root} (--det {a.det}) {' '.join(a.filters)}\n"
          f"CMC: {a.gmc or '끔 (켬 조합을 보려면 --gmc)'}\n결과: {out}\n")

    res = {}
    n_total, k = len(COMBOS) * len(modes), 0
    for mode, gmc in modes:
        for i, (name, changes) in enumerate(COMBOS):
            k += 1
            slug = f"{i:02d}_" + re.sub(r"[^0-9A-Za-z]+", "_", name).strip("_")
            cfg = os.path.join(out, "configs", slug + ".yaml")
            write_yaml(cfg, changes)
            cmd = [bench, a.root, "--det", a.det, "--config", cfg] + (["--gmc", gmc] if gmc else []) + a.filters
            log = os.path.join(out, "logs", f"{'cmc_' if gmc else ''}{slug}.txt")
            r = res[(mode, name)] = run(cmd, log)
            print(f"  [{k:2d}/{n_total}] {mode} {name:<22} TP {r['tp']:7,}  헛출력 {r['fp']:6,}  누락 {r['fn']:7,}  "
                  f"재현율 {pct(r['tp'], r['gt']):5.1f}%  정밀도 {pct(r['tp'], r['pred']):5.1f}%"
                  + (f"  IDSW {r['idsw']:5d}  IDF1 {r['idf1']:5.1f}%" if a.ids else ""))

    with open(os.path.join(out, "raw.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["cmc", "setting", "GT", "pred", "TP", "FP", "FN", "recall", "precision"]
                   + (["IDSW", "IDF1", "MOTA"] if a.ids else []))
        for (mode, name), r in res.items():
            w.writerow([mode, name, r["gt"], r["pred"], r["tp"], r["fp"], r["fn"],
                        f"{pct(r['tp'], r['gt']):.2f}", f"{pct(r['tp'], r['pred']):.2f}"]
                       + ([r["idsw"], r["idf1"], r["mota"]] if a.ids else []))

    gt = next(iter(res.values()))["gt"]
    head = ["설정", "pred", "TP ↑", "헛출력 ↓", "누락 ↓", "재현율 ↑", "정밀도 ↑"] + (["IDSW ↓", "IDF1 ↑", "MOTA ↑"] if a.ids else [])
    md = [f"# 조합 비교 – {a.root} (--det {a.det}) {' '.join(a.filters)}", "",
          f"GT {gt:,}개. TP = GT − 누락 = pred − 헛출력, 재현율 = TP/GT, 정밀도 = TP/pred. "
          "C1 = fuse_score + buffer 50 + L1. 괄호는 같은 CMC 조건의 기본값 대비 변화.",
          "" if a.ids else "정답에 track ID가 없어 IDSW·IDF1·MOTA는 넣지 않았다.", ""]
    for mode, _ in modes:
        ref = res[(mode, "기본값")]
        md += [f"## {mode}", "", "| " + " | ".join(head) + " |", "|---" + "|---:" * (len(head) - 1) + "|"]
        for name, _ in COMBOS:
            r = res[(mode, name)]
            rc, pr = pct(r["tp"], gt), pct(r["tp"], r["pred"])
            if name == "기본값":
                cells = [f"{r['pred']:,}", f"{r['tp']:,}", f"{r['fp']:,}", f"{r['fn']:,}", f"{rc:.1f}%", f"{pr:.1f}%"]
                cells += [f"{r['idsw']:,}", f"{r['idf1']:.1f}%", f"{r['mota']:.1f}%"] if a.ids else []
            else:
                cells = [d(r["pred"], ref["pred"]), d(r["tp"], ref["tp"]), d(r["fp"], ref["fp"]), d(r["fn"], ref["fn"]),
                         d(rc, pct(ref["tp"], gt)), d(pr, pct(ref["tp"], ref["pred"]))]
                cells += [d(r["idsw"], ref["idsw"]), d(r["idf1"], ref["idf1"]), d(r["mota"], ref["mota"])] if a.ids else []
            md.append(f"| {name} | " + " | ".join(cells) + " |")
        md.append("")
    if len(modes) == 2:
        md += ["## CMC 효과 (CMC O − CMC X, 같은 조합끼리)", "",
               "| 설정 | TP | 헛출력 | 누락 | 정밀도" + (" | IDSW | IDF1" if a.ids else "") + " |",
               "|---|---:|---:|---:|---:" + ("|---:|---:" if a.ids else "") + "|"]
        for name, _ in COMBOS:
            x, o = res[("CMC X", name)], res[("CMC O", name)]
            row = [f"{o['tp'] - x['tp']:+,}", f"{o['fp'] - x['fp']:+,}", f"{o['fn'] - x['fn']:+,}",
                   f"{pct(o['tp'], o['pred']) - pct(x['tp'], x['pred']):+.1f}"]
            row += [f"{o['idsw'] - x['idsw']:+,}", f"{o['idf1'] - x['idf1']:+.1f}"] if a.ids else []
            md.append(f"| {name} | " + " | ".join(row) + " |")
        md.append("")
    with open(os.path.join(out, "summary.md"), "w") as f:
        f.write("\n".join(md))
    print("\n" + "\n".join(md))
    print(f"\n저장: {out}/summary.md, {out}/raw.csv")
    return 0


if __name__ == "__main__":
    sys.exit(main())
