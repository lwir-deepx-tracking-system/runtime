#!/usr/bin/env python3
"""대상자(실험 인원)만 정답으로 남겨 TrackEval로 평가한다. 여러 시퀀스를 한 번에 평가해 COMBINED(전체)도 낸다.

  python3 tests/tracking/subject_eval.py <benchmark 실행 폴더> --subjects subjects.txt
      [--dataset-root /mnt/lwir_data/datasets] [--trackeval ~/workspace/TrackEval] [--out <작업 폴더>]

subjects.txt (시퀀스마다 대상자 GT ID, 쉼표로 여러 명):
  seq_001: 1
  seq_002: 1,2,3
  # 주석과 빈 줄은 무시. 목록에 없는 시퀀스는 평가하지 않는다.

방법
  - 대상자가 아닌 사람의 정답 행은 클래스를 8(distractor)로 바꾼다. TrackEval 전처리가
    그 사람과 IoU 0.5 이상 겹치는 Tracker 출력을 평가 전에 지우고, 그 사람의 정답도 평가에서 뺀다.
  - 그래서 남는 지표는 대상자 기준이다. 다만 아무 정답과도 겹치지 않는 출력(헛검출, 정답이 빈 구간의 출력)은
    지워지지 않고 헛출력으로 남으므로, 정밀도·MOTA·IDP는 대상자 외 요인도 섞인다.
    대상자 추적 품질은 IDR, AssA, AssRe, IDSW, Frag, MT, CLR_Re 위주로 본다.

결과: 화면 표 + <out>/subject_summary.csv  (기본 out = <실행 폴더>/subject_eval)
"""
import argparse
import csv
import os
import re
import shutil
import sys


def read_subjects(path):
    subjects = {}
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        m = re.fullmatch(r"(seq_\d+)\s*[:=]\s*([\d\s,]+)", line)
        if not m:
            sys.exit(f"{path}:{n}: 형식은 'seq_001: 1,2' 입니다 → {line!r}")
        ids = {int(x) for x in m.group(2).replace(" ", "").split(",") if x}
        if not ids:
            sys.exit(f"{path}:{n}: 대상자 ID가 없습니다")
        subjects[m.group(1)] = ids
    return subjects


def write_subject_gt(src, dst, ids):
    """대상자 행은 그대로(클래스 1), 나머지는 클래스 8(distractor). 반환: (대상자 행 수, 대상자 ID 중 실제로 있는 것, 마지막 프레임)"""
    kept, found, last = 0, set(), 0
    with open(src) as fi, open(dst, "w") as fo:
        for line in fi:
            p = [s.strip() for s in line.strip().split(",")]
            if len(p) < 6:
                continue
            while len(p) < 9:
                p.append("1" if len(p) < 8 else "-1")
            f, tid = int(float(p[0])), int(float(p[1]))
            last = max(last, f)
            if tid in ids:
                p[6], p[7] = "1", "1"
                kept += 1
                found.add(tid)
            else:
                p[7] = "8"
            fo.write(",".join(p[:9]) + "\n")
    return kept, found, last


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", help="lwir_benchmark 실행 폴더 (tracks/가 있는 곳)")
    ap.add_argument("--subjects", required=True)
    ap.add_argument("--dataset-root", default="/mnt/lwir_data/datasets")
    ap.add_argument("--trackeval", default=os.path.expanduser("~/workspace/TrackEval"))
    ap.add_argument("--out")
    a = ap.parse_args()

    sys.path.insert(0, a.trackeval)
    import numpy as np
    for name, typ in (("int", int), ("float", float), ("bool", bool)):  # 오래된 TrackEval의 np.int 등 호환
        if not hasattr(np, name):
            setattr(np, name, typ)
    try:
        import trackeval
    except ImportError:
        sys.exit(f"TrackEval을 찾지 못했습니다: --trackeval {a.trackeval}")

    subjects = read_subjects(a.subjects)
    tracks_dir = os.path.join(a.run_dir, "tracks")
    run_name = os.path.basename(os.path.normpath(a.run_dir))
    out = a.out or os.path.join(a.run_dir, "subject_eval")
    work = os.path.join(out, "work")
    shutil.rmtree(work, ignore_errors=True)
    gt_root = os.path.join(work, "gt", "LWIR-subject")
    trk_root = os.path.join(work, "trackers", "LWIR-subject", run_name, "data")
    os.makedirs(trk_root)

    seqs = []
    for seq, ids in sorted(subjects.items()):
        gt_src = os.path.join(a.dataset_root, seq, "gt", "gt.txt")
        trk_src = os.path.join(tracks_dir, seq + ".txt")
        if not os.path.exists(gt_src) or not os.path.exists(trk_src):
            print(f"  {seq}: 정답 또는 tracks 파일이 없어 건너뜀")
            continue
        os.makedirs(os.path.join(gt_root, seq, "gt"))
        kept, found, last = write_subject_gt(gt_src, os.path.join(gt_root, seq, "gt", "gt.txt"), ids)
        missing = ids - found
        if missing:
            print(f"  ※ {seq}: 정답에 없는 대상자 ID {sorted(missing)}")
        if kept == 0:
            print(f"  {seq}: 대상자 정답이 0줄이라 건너뜀")
            shutil.rmtree(os.path.join(gt_root, seq))
            continue
        last = max(last, max((int(float(l.split(",")[0])) for l in open(trk_src) if l.strip()), default=0))
        with open(os.path.join(gt_root, seq, "seqinfo.ini"), "w") as f:
            f.write(f"[Sequence]\nname={seq}\nseqLength={last}\n")
        shutil.copyfile(trk_src, os.path.join(trk_root, seq + ".txt"))
        seqs.append(seq)
        print(f"  {seq}: 대상자 {sorted(found)} → 정답 {kept}줄")
    if not seqs:
        sys.exit("평가할 시퀀스가 없습니다")
    seqmap = os.path.join(work, "seqmap.txt")
    with open(seqmap, "w") as f:
        f.write("name\n" + "\n".join(seqs) + "\n")

    ev_cfg = trackeval.Evaluator.get_default_eval_config()
    ev_cfg.update({"PRINT_RESULTS": False, "PRINT_CONFIG": False, "OUTPUT_SUMMARY": False,
                   "OUTPUT_DETAILED": False, "PLOT_CURVES": False, "TIME_PROGRESS": False})
    ds_cfg = trackeval.datasets.MotChallenge2DBox.get_default_dataset_config()
    ds_cfg.update({"GT_FOLDER": os.path.join(work, "gt"), "TRACKERS_FOLDER": os.path.join(work, "trackers"),
                   "BENCHMARK": "LWIR", "SPLIT_TO_EVAL": "subject", "TRACKERS_TO_EVAL": [run_name],
                   "SEQMAP_FILE": seqmap, "DO_PREPROC": True, "PRINT_CONFIG": False})
    metrics = [trackeval.metrics.HOTA(), trackeval.metrics.CLEAR({"PRINT_CONFIG": False}),
               trackeval.metrics.Identity({"PRINT_CONFIG": False})]
    res, _ = trackeval.Evaluator(ev_cfg).evaluate([trackeval.datasets.MotChallenge2DBox(ds_cfg)], metrics)
    res = res["MotChallenge2DBox"][run_name]

    cols = [("HOTA", "HOTA", "HOTA", 100), ("AssA", "HOTA", "AssA", 100), ("AssRe", "HOTA", "AssRe", 100),
            ("DetA", "HOTA", "DetA", 100), ("IDF1", "Identity", "IDF1", 100), ("IDR", "Identity", "IDR", 100),
            ("IDSW", "CLEAR", "IDSW", 1), ("Frag", "CLEAR", "Frag", 1), ("MT", "CLEAR", "MT", 1),
            ("Re", "CLEAR", "CLR_Re", 100), ("Pr", "CLEAR", "CLR_Pr", 100), ("MOTA", "CLEAR", "MOTA", 100)]

    def val(seq, group, key, scale):
        v = res[seq]["pedestrian"][group][key]
        v = float(np.mean(v)) if hasattr(v, "__len__") else float(v)
        return v * scale

    rows = []
    for seq in seqs + ["COMBINED_SEQ"]:
        rows.append([("전체" if seq == "COMBINED_SEQ" else seq)] + [val(seq, g, k, s) for _, g, k, s in cols])
    print(f"\n[대상자 기준 평가] {run_name}  (Re·Pr·MOTA는 대상자 외 헛출력이 섞임)")
    print(f"{'시퀀스':<9}" + "".join(f"{c[0]:>8}" for c in cols))
    for r in rows:
        print(f"{r[0]:<9}" + "".join(f"{v:>8.0f}" if c[3] == 1 else f"{v:>8.1f}" for v, c in zip(r[1:], cols)))
    with open(os.path.join(out, "subject_summary.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["sequence"] + [c[0] for c in cols])
        for r in rows:
            w.writerow([r[0]] + [f"{v:.3f}" for v in r[1:]])
    print(f"\n저장: {os.path.join(out, 'subject_summary.csv')}")


if __name__ == "__main__":
    main()
