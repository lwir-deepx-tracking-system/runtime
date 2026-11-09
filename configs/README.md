# ByteTrack experiment presets

These presets preserve the tuning experiments from `feature/bytetrack`.
They contain raw ByteTrack parameters for the CPU MOT evaluation tool:

```bash
./build/test/bench_mot17 /path/to/MOT17 --config configs/bt_flir_oc3.yaml
```

`bt_best*` and `bt_flir*` are experiment names, not a verified accuracy ranking.
The `oru`, `ocm`, `ocr`, and `oc3` suffixes select individual observation-based
extensions or their combination inside ByteTrack. They do not select a separate
OC-SORT tracker.

Runtime and Benchmark keep the pipeline configuration contract:
`tracking.id` selects a `tracking_catalog` entry whose file has a `tracking:`
mapping and `type: bytetrack`. The shared `config/tracking/bytetrack.yaml` uses
Buffered IoU (`buffer_scale: 0.5`) and OCR, with ORU, OCM, score fusion, NSA,
and buffer snap disabled. It uses `xywh`, no predicted output, and the
pipeline thresholds. Buffered IoU and OCR follow experiment 12 in the
[November 6 real-data evaluation](https://app.notion.com/p/4109de3f14a28064997ad50226e5714d),
which reported the highest aggregate IDF1 and MOTA among its tested settings.
XYWH is an additional selection for the current runtime. The evaluation does not
record all baseline thresholds, so this is not an exact reproduction. Its GT was
model-generated without human verification, CMC was
disabled, and current-model/hardware accuracy still needs verification.
Set `buffer_scale: 0` and `ocr: false` to disable these extensions.

To use an experiment preset in Runtime or Benchmark, wrap its parameters in
`tracking:`, add `type: bytetrack`, save it under `config/tracking/`, and reference
it from the catalog. The MOT tool accepts both raw presets and wrapped tracker
files, including `config/tracking/bytetrack.yaml`.

The standalone OC-SORT presets (`oc_flir.yaml`, `oc_tuned.yaml`) are excluded
from this integration because the standalone tracker is outside its scope.

Subject evaluation tools retained under `tests/tracking/`:

- `pick_subjects.py`: export frames and map labeled boxes to GT subject IDs.
- `subject_eval.py`: run TrackEval for selected subjects; requires NumPy and
  a separate TrackEval installation.
- `target_eval.py`: inspect one or more subjects, with optional GT/detection
  data, and compare tracking result files. GT-free identity events are estimates.
- `subjects.txt`: dataset-specific subject selections to review before reuse.

Use each tool's `--help` for its input paths. Actual dataset evaluations and NPU
inference are not established by the CPU tracker tests.
