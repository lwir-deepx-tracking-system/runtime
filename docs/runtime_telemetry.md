# Runtime frame telemetry

Implementation follows [Runtime–GUI 통계 로그 연동](https://app.notion.com/p/4139de3f14a2805aaca5c2e7d4185c05)
and PC `feat/runtime-telemetry`'s `RuntimeTelemetry.h`, `TrackingMetadata.cpp`
and `tests/fixtures/runtime-telemetry.hex`. The fixture is copied byte-for-byte
into Runtime tests. The default-enabled decision supersedes the design page's
original default-disabled proposal.

## Configuration

```yaml
gui:
  metadata:
    enabled: true
    host: <PC IPv4>
    port: 5001
    max_packet_bytes: 1200
    telemetry:
      enabled: true
```

AppConfig owns parsing and validation. Omitting `telemetry` or its `enabled`
key defaults to true. Deploy with the PC telemetry branch or a successor that
includes its parser. The old PC parser rejects a packet containing a trailer.
Disable telemetry explicitly when using the old application.

Timing collection is enabled when CSV recording is enabled **or** GUI metadata
telemetry is active. Only `measurement.enabled` creates a `MetricsWriter` and
files. GUI telemetry alone never enables CSV or requires an output directory.
Disabling metadata or the entire GUI disables telemetry collection unless CSV
still requires measurements.

## Collection and lifetime

`FrameTelemetryCollector` implements `IMeasurementSink`, forwarding original
measurements to the optional CSV sink before collecting compact telemetry events.
Camera and Detection are associated with model generation and capture frame ID.
Tracking binds that frame to the actual tracker ID, epoch and capture timestamp.
Tracking results remain immutable after publication.

The event ring holds 2048 fixed-size records. Measurement producers, including
the RTP probe, use try-lock enqueue and drop telemetry on contention/full capacity;
they do not wait for a consumer. Lifecycle activation is a short serialized
enqueue that retains the activation event even when capacity is exhausted.
Only GuiSender drains and aggregates. There is no file/network I/O or frame
aggregation in the probe. At most 256 frames are retained, with a 2-second TTL.
Dropped events and expired/evicted entries are telemetry counters, not network
loss estimates. Missing critical events leave a frame incomplete.

Before appsrc push, the sender registers its 90-kHz RTP timestamp against the
frame and encoding observation start. The probe publishes the final RTP marker
timestamp and steady-clock completion time. Submission return and marker may
arrive in either order. A sample completes after camera, detection, tracking,
GUI conversion/registration, successful submission and marker observations are
present. Control and detailed inference timings are optional and never delay
completion. Values are frozen at completion; late optional values do not mutate
an already observable sample.

At metadata serialization, the sender selects the newest completed earlier frame
from the same session, model generation and tracker epoch. No completed sample
means no trailer. Repeated samples are allowed; PC deduplicates them. Incomplete
N-1 does not prevent selecting an older completed frame. Late completion cannot
regress the newest sample. The last frame may not be delivered as telemetry
without subsequent metadata; the full existing Runtime CSV remains separate.

Model/tracker transitions invalidate the prior context and reject late old events.
Bypass clears retained samples. Pipeline creation/restart assigns a new session
ID and clears RTP mappings. The application owns the collector until all worker
and GStreamer probe lifetimes have ended.

## Fixed wire format

The base packet stays `28-byte LWIR header + 25-byte tracks`. When present, its
trailer is exactly 124 bytes. All integers and IEEE-754 float32 values are big
endian; durations are milliseconds.

| Trailer offset | Field | Bytes |
| --- | --- | --- |
| 0 | `LWTM` magic | 4 |
| 4 | session ID | 8 |
| 12 | model generation | 8 |
| 20 | tracker epoch | 8 |
| 28 | measured capture frame ID | 8 |
| 36 | validity mask, bits 0–17 only | 4 |
| 40 | sample age: current steady time minus measured capture completion | 4 |
| 44 | model ID | 4 |
| 48 | tracker ID | 4 |
| 52 | 18 timing values, in the order below | 72 |

Session, measured frame, model and tracker IDs must be positive. Generations and
epochs may be zero. Measured frame ID must be smaller than the carrier frame ID.
Valid values and sample age must be finite and nonnegative. An invalid slot is
encoded as zero but remains **unmeasured**, distinguished from valid measured
zero by its mask bit. NPU core requires `npu_profiler_valid`.

| Bit / slot | Timing |
| --- | --- |
| 0 | camera capture call |
| 1 | detection input queue |
| 2 | detection submit to callback |
| 3 | preprocessing |
| 4 | inference RunAsync to callback |
| 5 | NPU profiler core |
| 6 | postprocessing |
| 7 | tracking input queue |
| 8 | tracking processing |
| 9 | capture completion to tracking completion |
| 10 | control input queue |
| 11 | control executor mailbox submission |
| 12 | capture completion to control mailbox submission |
| 13 | GUI input queue |
| 14 | GUI LWIR-to-display conversion |
| 15 | GstBuffer allocation/copy through appsrc push return |
| 16 | RTP registration through final marker observation |
| 17 | capture completion through final RTP marker observation |

The durations overlap; do not add all slots. Encode+RTP includes downstream
queueing/conversion/encoding/packetization rather than pure encoder time. Capture
completion is not sensor exposure. Control submission is not serial transmission
or physical motor response. No host clock subtraction or one-way network latency
is inferred. Runtime timing and PC receive-to-paint timing remain separate.

At 1200 bytes, telemetry reserves room for 41 display tracks whether or not a
sample is ready. Selected-target priority and stable remaining input order are
preserved. Only displayed tracks are capped; tracking/control results are intact.
No fragmentation is introduced. Invalid lengths, magic, identities, mask or valid
values reject the whole packet before committing decoded output.

## Verification

`runtime_telemetry` covers the shared PC exact bytes, float endianness, malformed
trailers, missing/zero values, stable capacity, selected-target priority, optional
late measurements, reverse completion, timestamp wrap, session/model/tracker
transitions, TTL and bounded capacity/overflow. Software JPEG/H.264 sender tests
exercise actual loopback metadata and GStreamer marker association with transport
fixtures. These fixtures are not hardware inference outputs.

Physical board integration, MPP marker mapping, DXNN loading, NPU inference,
tensor validation, NPU benchmarks and physical gimbal response require separate
hardware execution and are unverified here.
