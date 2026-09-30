#!/usr/bin/env python3
"""bench_mot17 결과를 공식 평가 도구(TrackEval)로 채점한다: HOTA, MOTA, IDF1 등.

사용법
  python3 eval_mot17.py <MOT17 train 폴더> <결과 폴더> [--trackeval TrackEval 폴더]
    <결과 폴더> : bench_mot17 --out 으로 지정한 폴더 (안에 <시퀀스>.txt)
  예) python3 tests/tracking/eval_mot17.py datasets/MOT17/train results/mot17/bytetrack

결과 폴더에 있는 시퀀스만 평가한다. 요약 표는 결과 폴더 안의
pedestrian_summary.txt, pedestrian_detailed.csv 에도 저장된다.
"""
import argparse
import os
import shutil
import sys
import tempfile

import numpy as np

# TrackEval은 numpy 1.24에서 없어진 np.float / np.int / np.bool 을 쓴다
for alias, real in (("float", float), ("int", int), ("bool", bool)):
    if not hasattr(np, alias):
        setattr(np, alias, real)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("gt", help="MOT17 train 폴더 (<시퀀스>/gt/gt.txt, seqinfo.ini)")
    p.add_argument("res", help="bench_mot17 결과 폴더 (<시퀀스>.txt)")
    p.add_argument("--trackeval", help="TrackEval 소스 폴더 (pip로 설치했으면 생략)")
    a = p.parse_args()

    if a.trackeval:
        sys.path.insert(0, os.path.abspath(a.trackeval))
    try:
        import trackeval
    except ImportError:
        sys.exit("trackeval을 찾을 수 없습니다. pip install 하거나 --trackeval 로 소스 폴더를 지정하세요")

    seqs = sorted(f[:-4] for f in os.listdir(a.res) if f.endswith(".txt") and f.startswith("MOT"))
    missing = [s for s in seqs if not os.path.isfile(os.path.join(a.gt, s, "gt", "gt.txt"))]
    if not seqs:
        sys.exit(f"결과 파일이 없습니다: {a.res}/<시퀀스>.txt")
    if missing:
        sys.exit(f"정답(gt.txt)이 없는 시퀀스: {', '.join(missing)}  (test 시퀀스는 채점할 수 없습니다)")

    # TrackEval이 기대하는 구조: <trackers>/<tracker 이름>/data/<시퀀스>.txt
    work = tempfile.mkdtemp(prefix="trackeval_")
    try:
        name = os.path.basename(os.path.normpath(a.res))
        os.makedirs(os.path.join(work, name, "data"))
        for s in seqs:
            shutil.copy(os.path.join(a.res, s + ".txt"), os.path.join(work, name, "data", s + ".txt"))
        seqmap = os.path.join(work, "seqmap.txt")
        with open(seqmap, "w") as f:
            f.write("name\n" + "\n".join(seqs) + "\n")

        eval_cfg = trackeval.Evaluator.get_default_eval_config()
        eval_cfg.update({"USE_PARALLEL": False, "PRINT_CONFIG": False, "TIME_PROGRESS": False,
                         "OUTPUT_SUMMARY": True, "OUTPUT_DETAILED": True, "PLOT_CURVES": False})
        ds_cfg = trackeval.datasets.MotChallenge2DBox.get_default_dataset_config()
        ds_cfg.update({"GT_FOLDER": a.gt, "TRACKERS_FOLDER": work, "TRACKERS_TO_EVAL": [name],
                       "BENCHMARK": "MOT17", "SPLIT_TO_EVAL": "train", "SKIP_SPLIT_FOL": True,
                       "SEQMAP_FILE": seqmap, "PRINT_CONFIG": False})
        metrics = [trackeval.metrics.HOTA(), trackeval.metrics.CLEAR({"PRINT_CONFIG": False}),
                   trackeval.metrics.Identity({"PRINT_CONFIG": False})]
        evaluator = trackeval.Evaluator(eval_cfg)
        results, _ = evaluator.evaluate([trackeval.datasets.MotChallenge2DBox(ds_cfg)], metrics)

        for f in ("pedestrian_summary.txt", "pedestrian_detailed.csv"):
            src = os.path.join(work, name, f)
            if os.path.isfile(src):
                shutil.copy(src, os.path.join(a.res, f))

        r = results["MotChallenge2DBox"][name]["COMBINED_SEQ"]["pedestrian"]
        print(f"\n[요약] 시퀀스 {len(seqs)}개  HOTA {100 * np.mean(r['HOTA']['HOTA']):.1f}  "
              f"MOTA {100 * r['CLEAR']['MOTA']:.1f}  IDF1 {100 * r['Identity']['IDF1']:.1f}  "
              f"IDSW {int(r['CLEAR']['IDSW'])}  FP {int(r['CLEAR']['CLR_FP'])}  FN {int(r['CLEAR']['CLR_FN'])}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
