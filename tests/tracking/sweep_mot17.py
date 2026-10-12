#!/usr/bin/env python3
"""개선 1차 실험(Try1~4)을 지정한 MOT17 시퀀스만으로 다시 돌리고, 문서와 같은 형식의 표를 만든다.

사용법 (저장소 루트에서)
  python3 tests/tracking/sweep_mot17.py datasets/MOT17 [--seqs 05 10 13] [--bench 실행파일] [--out 폴더] [--buffer] [--gmc GMC폴더]
    --buffer: Buffered IoU 실험(Try 5)도 돌린다
    --gmc   : 모든 실험에 카메라 움직임 보정(CMC)을 켠다 (예: --gmc datasets/MOT17_gmc)
    --seqs  : 평가할 시퀀스 번호 (기본 05 10 13 = 이동 카메라 중 3개)
    --bench : bench_mot17 경로 (기본: build/ 아래에서 자동으로 찾음)
    --out   : 설정 YAML, 원본 출력, 결과 표를 저장할 폴더 (기본 results/sweep_<시퀀스>)

결과
  <out>/report.md  : Try1~4 표 (절대값 + 기준 대비 변화) + 최종 조합의 시퀀스별 비교
  <out>/raw.csv    : 설정 × 시퀀스(+합계)별 원시 수치
  <out>/configs/   : 실험마다 쓴 YAML
  <out>/logs/      : bench_mot17 출력 그대로

수치는 bench_mot17의 빠른 지표(IoU 0.5)이며 문서의 표와 같은 기준이다.
"""
import argparse
import csv
import glob
import os
import re
import subprocess
import sys

BASE = {"track_threshold": 0.5, "match_threshold": 0.8, "track_buffer": 30}

# (이름, 기본값에서 바꾼 키). 순서대로 실행한다
EXPERIMENTS = [
    ("기본값", {}),
    # Try1: 임계값
    ("low005", {"low_threshold": 0.05}),
    ("t04_n05", {"track_threshold": 0.4, "new_track_threshold": 0.5}),
    ("t03_n04", {"track_threshold": 0.3, "low_threshold": 0.05, "new_track_threshold": 0.4}),
    # Try2: track_buffer
    ("buffer 35", {"track_buffer": 35}),
    ("buffer 40", {"track_buffer": 40}),
    ("buffer 50", {"track_buffer": 50}),
    ("buffer 60", {"track_buffer": 60}),
    ("buffer 90", {"track_buffer": 90}),
    # Try3: fuse_score
    ("fuse_score", {"fuse_score": True}),
    ("fuse + buffer 50", {"fuse_score": True, "track_buffer": 50}),
    # Try4: lost_output_frames (기준: fuse + buffer 50)
    ("+ L1", {"fuse_score": True, "track_buffer": 50, "lost_output_frames": 1}),
    ("+ L2", {"fuse_score": True, "track_buffer": 50, "lost_output_frames": 2}),
    ("+ L5", {"fuse_score": True, "track_buffer": 50, "lost_output_frames": 5}),
    ("+ L10", {"fuse_score": True, "track_buffer": 50, "lost_output_frames": 10}),
    # Try4b: L1 + fuse 조합에서 track_buffer 비교
    ("L1 + fuse + buffer 35", {"fuse_score": True, "track_buffer": 35, "lost_output_frames": 1}),
    ("L1 + fuse + buffer 50", {"fuse_score": True, "track_buffer": 50, "lost_output_frames": 1}),
    ("L1 + fuse + buffer 60", {"fuse_score": True, "track_buffer": 60, "lost_output_frames": 1}),
]

# --buffer 를 주면 추가로 돌린다 (Buffered IoU 코드가 들어간 빌드에서만)
BEST = {"fuse_score": True, "track_buffer": 50, "lost_output_frames": 1}
BUFFER_EXPERIMENTS = [
    ("biou 0.3", {"buffer_scale": 0.3}),
    ("biou 0.5", {"buffer_scale": 0.5}),
    ("biou 1.0", {"buffer_scale": 1.0}),
    ("biou 0.5 작은 표적(≤32px)만", {"buffer_scale": 0.5, "buffer_small_size": 32}),
    ("biou 0.5 + snap", {"buffer_scale": 0.5, "buffer_snap": True}),
    ("+ L1 + biou 0.3", dict(BEST, buffer_scale=0.3)),
    ("+ L1 + biou 0.5", dict(BEST, buffer_scale=0.5)),
    ("+ L1 + biou 0.5 + snap", dict(BEST, buffer_scale=0.5, buffer_snap=True)),
]
BUFFER_TRY = [
    ("Try 5 – Buffered IoU (단독)", "기본값",
     ["biou 0.3", "biou 0.5", "biou 1.0", "biou 0.5 작은 표적(≤32px)만", "biou 0.5 + snap"]),
    ("Try 5b – Buffered IoU + 최종 조합", "+ L1", ["+ L1 + biou 0.3", "+ L1 + biou 0.5", "+ L1 + biou 0.5 + snap"]),
]

TRY = [
    ("Try 1 – low / track / new 임계값", "기본값", ["low005", "t04_n05", "t03_n04"]),
    ("Try 2 – track_buffer", "기본값", ["buffer 35", "buffer 40", "buffer 50", "buffer 60", "buffer 90"]),
    ("Try 3 – fuse_score (cost = 1 − IoU × 검출 점수)", "기본값", ["buffer 50", "fuse_score", "fuse + buffer 50"]),
    ("Try 4 – 놓친 Track을 예측 위치로 N프레임 출력", "fuse + buffer 50", ["+ L1", "+ L2", "+ L5", "+ L10"]),
    ("Try 4b – L1 + fuse에서 track_buffer 비교", "기본값",
     ["L1 + fuse + buffer 35", "L1 + fuse + buffer 50", "L1 + fuse + buffer 60"]),
]
FINAL = "+ L1"  # 문서의 최종 조합: fuse + buffer 50 + L1

ROW = re.compile(r"^\s+(MOT17-\S+|합계)\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+(\d+)\s+([\d.]+)%\s+(-?[\d.]+)%\s+([\d.]+)")


def find_bench():
    hits = [p for p in glob.glob("build/**/bench_mot17", recursive=True) if os.access(p, os.X_OK)]
    if not hits:
        sys.exit("bench_mot17을 찾지 못했습니다. 먼저 빌드하거나 --bench로 경로를 지정하세요\n"
                 "  cmake --build build/cpu --target bench_mot17")
    return max(hits, key=os.path.getmtime)


def write_yaml(path, changes):
    cfg = dict(BASE, **changes)
    with open(path, "w") as f:
        for k, v in cfg.items():
            f.write(f"{k}: {str(v).lower() if isinstance(v, bool) else v}\n")


def run(bench, mot, seqs, cfg, log, gmc=None):
    extra = ["--gmc", gmc] if gmc else []
    out = subprocess.run([bench, mot, "--config", cfg, *extra, *seqs], capture_output=True, text=True)
    with open(log, "w") as f:
        f.write(out.stdout + out.stderr)
    if out.returncode != 0:
        sys.exit(f"bench_mot17 실패 ({cfg}). 로그: {log}\n{out.stderr[-800:]}")
    rows = {}
    for line in out.stdout.splitlines():
        m = ROW.match(line)
        if m:
            name = m.group(1)
            gt, pred, idsw, fp, fn = (int(m.group(i)) for i in range(2, 7))
            rows[name] = dict(gt=gt, pred=pred, idsw=idsw, fp=fp, fn=fn, idf1=float(m.group(7)),
                              mota=float(m.group(8)), ms=float(m.group(9)))
    if "합계" not in rows:  # 시퀀스가 1개면 합계 줄이 없을 수 있다
        only = [v for k, v in rows.items() if k.startswith("MOT17")]
        if len(only) == 1:
            rows["합계"] = only[0]
    if "합계" not in rows:
        sys.exit(f"결과 표를 읽지 못했습니다. 로그: {log}")
    return rows


def rc(r):
    return 100.0 * (r["gt"] - r["fn"]) / r["gt"]


def chg(v, ref, pct=True):
    d = v - ref
    if not pct or ref == 0:
        return f"{d:+,}"
    return f"{d:+,} ({100.0 * d / ref:+.1f}%)"


def table(head, rows):
    out = ["| " + " | ".join(head) + " |", "|" + "|".join("---" for _ in head) + "|"]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mot17", help="MOT17 폴더 (train/ 이 들어 있는 폴더)")
    p.add_argument("--seqs", nargs="+", default=["05", "10", "13"])
    p.add_argument("--bench")
    p.add_argument("--out")
    p.add_argument("--buffer", action="store_true", help="Buffered IoU 실험(Try 5)도 돌린다")
    p.add_argument("--gmc", help="카메라 움직임 보정(CMC)을 켠다. GMC-MOT17-XX.txt가 있는 폴더 (예: datasets/MOT17_gmc)")
    a = p.parse_args()
    experiments = EXPERIMENTS + (BUFFER_EXPERIMENTS if a.buffer else [])
    tries = TRY + (BUFFER_TRY if a.buffer else [])

    bench = a.bench or find_bench()
    out = a.out or f"results/sweep_{'_'.join(a.seqs)}" + ("_cmc" if a.gmc else "")
    os.makedirs(os.path.join(out, "configs"), exist_ok=True)
    os.makedirs(os.path.join(out, "logs"), exist_ok=True)
    print(f"bench: {bench}\n시퀀스: {' '.join(a.seqs)}\nCMC: {a.gmc or '끔'}\n결과: {out}")

    res = {}
    for i, (name, changes) in enumerate(experiments):
        slug = f"{i:02d}_" + re.sub(r"[^0-9A-Za-z]+", "_", name).strip("_")
        cfg = os.path.join(out, "configs", slug + ".yaml")
        write_yaml(cfg, changes)
        res[name] = run(bench, a.mot17, a.seqs, cfg, os.path.join(out, "logs", slug + ".txt"), a.gmc)
        t = res[name]["합계"]
        print(f"  [{i + 1:2d}/{len(experiments)}] {name:<22} IDSW {t['idsw']:5d}  헛출력 {t['fp']:6d}  "
              f"누락 {t['fn']:6d}  IDF1 {t['idf1']:5.1f}%  MOTA {t['mota']:5.1f}%")

    with open(os.path.join(out, "raw.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["setting", "sequence", "GT", "pred", "IDSW", "FP", "FN", "IDF1", "MOTA", "recall", "ms_per_frame"])
        for name, rows in res.items():
            for s, r in rows.items():
                w.writerow([name, s, r["gt"], r["pred"], r["idsw"], r["fp"], r["fn"], r["idf1"], r["mota"],
                            f"{rc(r):.2f}", r["ms"]])

    seq_names = sorted(k for k in res["기본값"] if k.startswith("MOT17"))
    md = [f"# 개선 1차 재검증 – 시퀀스 {', '.join(a.seqs)}", "",
          f"대상: {', '.join(seq_names)} (합계 기준). bench_mot17 빠른 지표, IoU 0.5. CMC: {a.gmc or '끔'}", ""]

    for title, ref_name, names in tries:
        ref = res[ref_name]["합계"]
        md += [f"## {title}", "", "### 검증 결과", ""]
        md.append(table(["설정", "GT", "pred", "IDSW", "헛출력", "누락", "IDF1", "MOTA", "재현율", "ms/frame"],
                        [[n, f"{r['gt']:,}", f"{r['pred']:,}", r["idsw"], f"{r['fp']:,}", f"{r['fn']:,}",
                          f"{r['idf1']:.1f}%", f"{r['mota']:.1f}%", f"{rc(r):.2f}%", f"{r['ms']:.3f}"]
                         for n in [ref_name] + names for r in [res[n]["합계"]]]))
        md += ["", f"### {ref_name} 대비 변화", ""]
        head = ["설정", "누락", "헛출력", "IDSW", "IDF1", "MOTA", "재현율"]
        if title.startswith("Try 4"):
            head.insert(3, "늘어난 출력 중 맞춘 비율")
        rows = [[f"{ref_name} (기준)", f"({ref['fn']:,})", f"({ref['fp']:,})"] +
                (["–"] if title.startswith("Try 4") else []) +
                [f"({ref['idsw']})", f"({ref['idf1']:.1f}%)", f"({ref['mota']:.1f}%)", f"{rc(ref):.2f}%"]]
        for n in names:
            r = res[n]["합계"]
            row = [n, chg(r["fn"], ref["fn"]), chg(r["fp"], ref["fp"])]
            if title.startswith("Try 4"):
                gained, added = ref["fn"] - r["fn"], r["pred"] - ref["pred"]
                row.append(f"{100.0 * gained / added:.0f}%" if added > 0 else "–")
            row += [chg(r["idsw"], ref["idsw"]), f"{r['idf1'] - ref['idf1']:+.1f}%p",
                    f"{r['mota'] - ref['mota']:+.1f}%p", f"{rc(r):.2f}%"]
            rows.append(row)
        md += [table(head, rows), ""]

    # 최종 조합의 시퀀스별 비교 (문서의 '검증' 표와 같은 형식)
    md += [f"## 최종 조합 (fuse + buffer 50 + L1) 시퀀스별 비교 – 기본값 대비", ""]
    rows = []
    for s in seq_names + ["합계"]:
        b, f_ = res["기본값"][s], res[FINAL][s]
        rows.append([s, f"{b['idsw']} → {f_['idsw']} ({f_['idsw'] - b['idsw']:+d})", f"{f_['fp'] - b['fp']:+,}",
                     f"{f_['fn'] - b['fn']:+,}", f"{f_['idf1'] - b['idf1']:+.1f}%p", f"{f_['mota'] - b['mota']:+.1f}%p"])
    md += [table(["시퀀스", "IDSW", "헛출력", "누락", "IDF1", "MOTA"], rows), ""]

    path = os.path.join(out, "report.md")
    with open(path, "w") as f:
        f.write("\n".join(md))
    print(f"\n표 저장: {path}\n원시 수치: {os.path.join(out, 'raw.csv')}")


if __name__ == "__main__":
    main()
