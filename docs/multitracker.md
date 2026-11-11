# Multi-Tracker configuration and output

`tracking.id` selects a catalog entry in both applications:

| ID | Config | Pipeline |
| --- | --- | --- |
| 1 | `bytetrack.yaml` | Detection → ByteTrack |
| 2 | `botsort.yaml` | Detection → RepVGG → BoT-SORT association |
| 3 | `bytetrack_post_reid.yaml` | Detection → ByteTrack → RepVGG → ReidMatcher |

The benchmark's existing `reid.enabled: true` keeps the legacy ID 1
post-tracking ReID behavior. Set it to `false` for ID 1 ByteTrack-only, or
select ID 2 or 3 with it set to `true`. The runtime uses the selected tracker
YAML's `post_reid` flag instead. ID 1 requires no RepVGG engine.

For ID 2, `appearance_max_crops_per_frame: 0` embeds every eligible detection.
A positive limit skips later detection indices; skipped detections use IoU-only
association. `appearance_min_crop_height` filters small crops. BoT-SORT uses
`1 - IoU` for spatial distance and `(1 - cosine) / 2` for appearance distance.
Appearance can improve the association cost only if both features exist, IoU
distance passes `proximity_threshold`, and appearance distance passes
`appearance_threshold`. The first association uses high-confidence detections;
the second uses low-confidence detections and IoU. Features are updated by a
normalized exponential moving average (`feature_alpha`). Missing features
leave the prior track feature intact. There is currently no camera-motion
estimate supplied to BoT-SORT.

The benchmark keeps MOTChallenge rows. `tracks/` contains raw ByteTrack IDs
or final BoT-SORT IDs. `tracks_reid/` contains final post-ReID IDs or the same
final BoT-SORT IDs. In `reid/*.csv`, the second field is `track_id` for
post-ReID and `detection_index` for BoT-SORT. `*_matches.csv` identifies
`post_reconnect` separately from BoT-SORT `high`, `low`, `confirm`, `new`,
`lost`, `removed`, and gate diagnostics. The timing table reports detection,
tracking/association, ReID inference, post-ReID matching, crop count, and
frame total. CSV writing and image loading are outside frame total, as before.

Runtime keeps the existing DetectionThread callback ordering, queues,
TrackerManager snapshot/epoch publication, model-generation reset, and
GUI/Control result contract. TrackerManager creates the selected composite
before committing a switch. The active tracker owns its RepVGG pipeline and
identity state, so reset and destruction follow the tracker generation.

The current runtime configuration binds both Detection and ReID to `all`
DX-M1 cores. Benchmark binds Detection to core 0 and ReID to core 1. Measure
the runtime resource and latency impact on the Orange Pi before changing core
binding or pipeline concurrency.
