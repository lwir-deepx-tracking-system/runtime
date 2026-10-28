#!/usr/bin/env python3
import csv
import math
import sys
from pathlib import Path

def read_rows(path, expected_header):
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != expected_header:
            raise SystemExit(f"{path}: unexpected header {reader.fieldnames!r}")
        rows = list(reader)
    if not rows:
        raise SystemExit(f"{path}: Runtime produced no data rows")
    return rows

def finite_number(value, field, path):
    try:
        number = float(value)
    except (TypeError, ValueError):
        raise SystemExit(f"{path}: invalid {field} value {value!r}")
    if not math.isfinite(number) or number < 0:
        raise SystemExit(f"{path}: invalid {field} value {value!r}")

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_runtime_output.py <runtime-result-dir>")
    result_dir = Path(sys.argv[1])
    for snapshot in ("runtime.yaml",):
        if not (result_dir / snapshot).is_file():
            raise SystemExit(f"missing Runtime snapshot: {snapshot}")

    metrics_path = result_dir / "metrics.csv"
    metrics = read_rows(
        metrics_path,
        ["frame_id", "stage", "queue_wait_ms", "processing_ms", "e2e_ms"])
    stages = {row["stage"] for row in metrics}
    required_stages = {"camera", "detection", "tracking"}
    if not required_stages.issubset(stages):
        raise SystemExit(f"{metrics_path}: missing stages {required_stages - stages}")
    for row in metrics:
        try:
            int(row["frame_id"])
        except ValueError:
            raise SystemExit(f"{metrics_path}: invalid frame_id {row['frame_id']!r}")
        for field in ("queue_wait_ms", "processing_ms", "e2e_ms"):
            if row[field]:
                finite_number(row[field], field, metrics_path)

    timing_path = result_dir / "detection_timing.csv"
    timings = read_rows(
        timing_path,
        ["frame_id", "preprocess_ms", "latency_ms",
         "npu_processing_ms", "postprocess_ms"])
    for row in timings:
        try:
            int(row["frame_id"])
        except ValueError:
            raise SystemExit(f"{timing_path}: invalid frame_id {row['frame_id']!r}")
        for field in ("preprocess_ms", "latency_ms",
                      "npu_processing_ms", "postprocess_ms"):
            finite_number(row[field], field, timing_path)
    print(f"validated {len(metrics)} stage rows and {len(timings)} detection rows")

if __name__ == "__main__":
    main()
