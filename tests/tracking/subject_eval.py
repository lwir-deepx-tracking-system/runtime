#!/usr/bin/env python3
"""TrackEval로 여러 시퀀스를 한 번에 평가해 시퀀스별 지표와 COMBINED(전체)를 낸다.
--subjects를 주면 대상자(실험 인원)만 정답으로 남긴 대상자 기준 평가, 주지 않으면 전체 정답 기준 평가다.

  전체 정답 기준 (Orange Pi mot_eval과 같은 지표, 전체 HOTA 포함)
  python3 tests/tracking/subject_eval.py <실행 폴더 또는 bench_mot17 --out 폴더> [--dataset-root ...]

  PC의 bench_mot17 결과: GT는 lwir_to_mot.py로 만든 폴더(<root>/train/seq_XXX-YOLO26S/gt/gt.txt)도 찾는다
  python3 tests/tracking/subject_eval.py results/lwir_oc/oc_16 --dataset-root datasets/LWIR_MOT --trackeval ~/TrackEval

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
import glob
import os
import re
import shutil
import sys


def find_gt(root, seq):
    """<root>/seq_001/gt/gt.txt (Orange Pi) 또는 <root>/train/seq_001-*/gt/gt.txt (lwir_to_mot.py 결과)"""
    for pattern in (os.path.join(root, seq, "gt", "gt.txt"),
                    os.path.join(root, "train", seq + "-*", "gt", "gt.txt"),
                    os.path.join(root, seq + "-*", "gt", "gt.txt")):
        found = sorted(glob.glob(pattern))
        if found:
            return found[0]
    return None


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
    """대상자 행은 그대로(클래스 1), 나머지는 클래스 8(distractor). ids가 None이면 전원을 정답으로 둔다.
    반환: (정답 행 수, 대상자 ID 중 실제로 있는 것, 마지막 프레임)"""
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
            if ids is None or tid in ids:
                p[6], p[7] = "1", "1"
                kept += 1
                found.add(tid)
            else:
                p[7] = "8"
            fo.write(",".join(p[:9]) + "\n")
    return kept, found, last


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", help="lwir_benchmark 실행 폴더 (tracks/가 있는 곳) 또는 bench_mot17 --out 폴더")
    ap.add_argument("--subjects", help="대상자 목록. 주지 않으면 전체 정답 기준으로 평가")
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

    tracks_dir = os.path.join(a.run_dir, "tracks")
    if not os.path.isdir(tracks_dir):
        tracks_dir = a.run_dir  # bench_mot17 --out 폴더처럼 결과 파일이 바로 들어 있는 경우
    # 결과 파일: seq_001.txt (lwir_benchmark) 또는 seq_001-YOLO26S.txt (bench_mot17)
    track_files = {}
    for f in sorted(glob.glob(os.path.join(tracks_dir, "seq_*.txt"))):
        m = re.fullmatch(r"(seq_\d+)(-[^/]*)?\.txt", os.path.basename(f))
        if m:
            track_files.setdefault(m.group(1), f)
    if a.subjects:
        subjects = read_subjects(a.subjects)
    else:
        subjects = {seq: None for seq in track_files}  # 전체 정답 기준
    if not subjects:
        sys.exit(f"{tracks_dir} 에 seq_XXX 결과 파일이 없습니다")
    mode = "대상자 기준" if a.subjects else "전체 정답 기준"
    run_name = os.path.basename(os.path.normpath(a.run_dir))
    out = a.out or os.path.join(a.run_dir, "subject_eval" if a.subjects else "all_eval")
    work = os.path.join(out, "work")
    shutil.rmtree(work, ignore_errors=True)
    gt_root = os.path.join(work, "gt", "LWIR-subject")
    trk_root = os.path.join(work, "trackers", "LWIR-subject", run_name, "data")
    os.makedirs(trk_root)

    seqs = []
    for seq, ids in sorted(subjects.items()):
        gt_src = find_gt(a.dataset_root, seq)
        trk_src = track_files.get(seq)
        if gt_src is None or trk_src is None:
            print(f"  {seq}: 정답 또는 tracks 파일이 없어 건너뜀")
            continue
        os.makedirs(os.path.join(gt_root, seq, "gt"))
        kept, found, last = write_subject_gt(gt_src, os.path.join(gt_root, seq, "gt", "gt.txt"), ids)
        missing = (ids - found) if ids is not None else set()
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
        who = f"대상자 {sorted(found)}" if ids is not None else f"전원 {len(found)}명"
        print(f"  {seq}: {who} → 정답 {kept}줄")
    if not seqs:
        sys.exit("평가할 시퀀스가 없습니다")
    seqmap = os.path.join(work, "seqmap.txt")
    with open(seqmap, "w") as f:
        f.write("name\n" + "\n".join(seqs) + "\n")

    ev_cfg = trackeval.Evaluator.get_default_eval_config()
    ev_cfg.update({"PRINT_RESULTS": False, "PRINT_CONFIG": False, "OUTPUT_SUMMARY": False,
                   "OUTPUT_DETAILED": False, "PLOT_CURVES": False, "TIME_PROGRESS": False,
                   "USE_PARALLEL": False})
    # 오류가 나면 조용히 넘어가지 말고 그 자리에서 멈추게 한다 (TrackEval 버전마다 기본값이 다름)
    for key, value in (("BREAK_ON_ERROR", True), ("RETURN_ON_ERROR", False), ("LOG_ON_ERROR", None)):
        if key in ev_cfg:
            ev_cfg[key] = value
    ds_cfg = trackeval.datasets.MotChallenge2DBox.get_default_dataset_config()
    ds_cfg.update({"GT_FOLDER": os.path.join(work, "gt"), "TRACKERS_FOLDER": os.path.join(work, "trackers"),
                   "BENCHMARK": "LWIR", "SPLIT_TO_EVAL": "subject", "TRACKERS_TO_EVAL": [run_name],
                   "SEQMAP_FILE": seqmap, "DO_PREPROC": True, "PRINT_CONFIG": False})
    metrics = [trackeval.metrics.HOTA(), trackeval.metrics.CLEAR({"PRINT_CONFIG": False}),
               trackeval.metrics.Identity({"PRINT_CONFIG": False})]
    print(f"\nTrackEval 평가 시작 ({len(seqs)}개 시퀀스, TrackEval: {os.path.dirname(trackeval.__file__)})", flush=True)
    res, msg = trackeval.Evaluator(ev_cfg).evaluate([trackeval.datasets.MotChallenge2DBox(ds_cfg)], metrics)
    ds_res = res.get("MotChallenge2DBox", {})
    if run_name not in ds_res or "COMBINED_SEQ" not in ds_res.get(run_name, {}):
        print("평가 결과가 비어 있습니다. TrackEval 메시지:", msg)
        sys.exit(1)
    res = ds_res[run_name]

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
    note = "  (Re·Pr·MOTA는 대상자 외 헛출력이 섞임)" if a.subjects else ""
    print(f"\n[{mode} 평가] {run_name}{note}")
    print(f"{'시퀀스':<9}" + "".join(f"{c[0]:>8}" for c in cols))
    for r in rows:
        print(f"{r[0]:<9}" + "".join(f"{v:>8.0f}" if c[3] == 1 else f"{v:>8.1f}" for v, c in zip(r[1:], cols)))
    with open(os.path.join(out, "subject_summary.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["sequence"] + [c[0] for c in cols])
        for r in rows:
            w.writerow([r[0]] + [f"{v:.3f}" for v in r[1:]])
    # Orange Pi mot_eval의 summary와 같은 39개 지표 전부 (비율은 %, 개수는 그대로)
    full = [("HOTA", k) for k in ("HOTA", "DetA", "AssA", "DetRe", "DetPr", "AssRe", "AssPr", "LocA", "OWTA",
                                  "HOTA(0)", "LocA(0)", "HOTALocA(0)")] + \
           [("CLEAR", k) for k in ("MOTA", "MOTP", "MODA", "CLR_Re", "CLR_Pr", "MTR", "PTR", "MLR", "CLR_TP",
                                   "CLR_FN", "CLR_FP", "IDSW", "MT", "PT", "ML", "Frag", "sMOTA")] + \
           [("Identity", k) for k in ("IDF1", "IDR", "IDP", "IDTP", "IDFN", "IDFP")] + \
           [("Count", k) for k in ("Dets", "GT_Dets", "IDs", "GT_IDs")]
    counts = {"CLR_TP", "CLR_FN", "CLR_FP", "IDSW", "MT", "PT", "ML", "Frag", "IDTP", "IDFN", "IDFP",
              "Dets", "GT_Dets", "IDs", "GT_IDs"}
    full_path = os.path.join(out, "metrics_full.csv")
    with open(full_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["sequence"] + [k for _, k in full])
        for seq in seqs + ["COMBINED_SEQ"]:
            vals = []
            for g, k in full:
                group = res[seq]["pedestrian"].get(g, {})
                if k not in group:
                    vals.append("")
                    continue
                v = group[k]
                v = float(np.mean(v)) if hasattr(v, "__len__") else float(v)
                vals.append(f"{v:.0f}" if k in counts else f"{v * 100:.3f}")
            w.writerow([("전체" if seq == "COMBINED_SEQ" else seq)] + vals)

    print(f"\n저장: {os.path.join(out, 'subject_summary.csv')} (주요 지표)")
    print(f"      {full_path} (TrackEval 지표 전체 {len(full)}개)")


if __name__ == "__main__":
    main()
