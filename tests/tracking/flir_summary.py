#!/usr/bin/env python3
"""bench_mot17을 실행하고 ID 없이 의미 있는 지표만 출력한다 (FLIR ADAS처럼 track ID가 없는 정답용).

  python3 tests/tracking/flir_summary.py datasets/FLIR_MOT --det SIM [--config yaml] [--gmc 폴더] [--bench 실행파일]

출력: GT, pred, TP(맞춤 = GT − 누락 = pred − 헛출력), 헛출력, 누락, 재현율(TP/GT), 정밀도(TP/pred)
IDSW·IDF1·MOTA는 정답에 track ID가 없어 의미가 없으므로 출력하지 않는다.
나머지 인자는 그대로 bench_mot17에 넘긴다.
"""
import glob
import os
import re
import subprocess
import sys

ROW = re.compile(r"^\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+[\d.]+%\s+-?[\d.]+%\s+[\d.]+\s*$")


def find_bench():
    # sweep_mot17.py와 같은 규칙: build/ 아래 실행 가능한 bench_mot17 중 가장 최근 것
    hits = [p for p in glob.glob("build/**/bench_mot17", recursive=True) if os.access(p, os.X_OK)]
    if not hits:
        sys.exit("bench_mot17을 찾지 못했습니다 (build/ 아래). repo 최상위 폴더에서 실행했는지, 빌드했는지 확인하세요\n"
                 "  cmake --build build/cpu --target bench_mot17\n"
                 "  또는 --bench <실행파일 경로>")
    return max(hits, key=os.path.getmtime)


def main():
    args = sys.argv[1:]
    bench = None
    if "--bench" in args:
        i = args.index("--bench")
        bench = args[i + 1]
        del args[i:i + 2]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    bench = bench or find_bench()
    res = subprocess.run([bench] + args, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout + res.stderr)
        return res.returncode

    rows = []
    for line in res.stdout.splitlines():
        m = ROW.match(line)
        if m:
            name, gt, pred, _idsw, fp, fn = m.group(1), *map(int, m.groups()[1:])
            rows.append((name, gt, pred, fp, fn))
        elif "처리 시간" in line or line.strip().startswith(("fuse_score", "[MOT17", "2차 매칭", "CMC")):
            print(line)
    if not rows:
        sys.stderr.write(res.stdout)
        sys.exit("bench 출력에서 결과 표를 찾지 못했습니다")

    print(f"\n  {'sequence':<20}{'GT':>8}{'pred':>8}{'TP':>8}{'FP':>8}{'FN':>8}{'recall':>9}{'precision':>11}")
    print("  (TP 맞춤 / FP 헛출력 / FN 누락 / recall 재현율 = TP/GT / precision 정밀도 = TP/pred)")
    print("  " + "-" * 78)
    for name, gt, pred, fp, fn in rows:
        tp = gt - fn
        if tp != pred - fp:
            print(f"  ! {name}: GT−누락({tp})과 pred−헛출력({pred - fp})이 다릅니다")
        if name == "합계":
            print("  " + "-" * 78)
        print(f"  {name:<20}{gt:>8}{pred:>8}{tp:>8}{fp:>8}{fn:>8}"
              f"{100.0 * tp / max(gt, 1):>8.1f}%{100.0 * tp / max(pred, 1):>10.1f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
