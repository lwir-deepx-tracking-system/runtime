# Runtime–PC communication

Runtime follows `pc-application`'s current `feat/gui-video` wire contract and the
adopted decisions in [Application 설계](https://app.notion.com/p/3f29de3f14a280829f90dc2dfd666d04).
The [1103 roadmap](https://app.notion.com/p/40c9de3f14a28074905bd2239d6e9fdb)
describes wider state/control/gimbal work; this change implements communication
with deferred actuation, not that whole roadmap. Its older branch/commit baseline
is superseded for this task by the user's `main` / `185df16` baseline.
`pc_v1` is the local configuration name; no version field is transmitted.
The PC repository is not modified by this change.

## Configuration and responsibilities

All YAML parsing/validation remains in AppConfig (`config_parser`). Components
receive typed configuration, never YAML nodes or filenames.

```yaml
gui:
  enabled: true
  video:
    host: <PC IPv4>
    port: 5000
    codec: h264
    encoder: mpph264enc
    # Keep bitrate_kbps, jpeg_quality, keyframe_interval, fps and rtp_mtu.
  metadata:
    enabled: true
    protocol: pc_v1
    host: <PC IPv4>
    port: 5001
    max_packet_bytes: 1200
  command:
    enabled: true
    protocol: pc_v1
    handler: deferred
    bind_address: 0.0.0.0
    port: 5002
    receive_timeout_ms: 1000
```

This is an excerpt; retain the other required keys in `config/runtime.yaml`.
On the PC, set `control.host` to the board IP and select the same video codec.
The checked-in runtime keeps GUI/control disabled and destinations on localhost.
JPEG remains selectable with `codec: jpeg` / `encoder: mppjpegenc`; Docker CPU
transport tests explicitly select `x264enc` or `jpegenc`. No automatic encoder
fallback is performed.

`GuiProtocolFactory` creates the `IGuiProtocol` wire strategy selected by
`metadata.protocol` / `command.protocol`. `GuiCommandHandlerFactory` creates the
`IGuiCommandHandler` application strategy selected by `command.handler`.
Unknown settings fail during configuration; omitted new keys default to
`pc_v1` / `deferred`. A new protocol/handler is added through the interface,
factory and typed configuration parser, without changing socket framing loops
or reconnecting the worker directly to target selection.

`GuiSender` owns video/metadata transport. `GuiReceiver` owns the TCP session,
validates framing before allocating/reading the body, and delegates replies to
the handler. `GuiReceiverThread` owns the worker lifecycle. Only the worker uses
the command handler; stop() interrupts network waits from another thread.

## Metadata UDP 5001

All multibyte fields are big endian; floats use IEEE-754 float32 bits.

| Offset | Field | Bytes |
|---|---|---|
| 0 | Magic `LWIR` (0x4c574952) | 4 |
| 4 | Capture frame ID, nonzero uint64 | 8 |
| 12 | RTP timestamp, uint32 | 4 |
| 16 | GUI send start, epoch microseconds, uint64 | 8 |
| 24 | Track count, uint32 | 4 |
| 28 | Repeated track records | 25 each |

Each track is `ID:uint32, x:float32, y:float32, width:float32,
height:float32, class:uint8, confidence:float32`, in that order.
Wire IDs retain the full uint32 range (1..4,294,967,295). Tracker int IDs are
adapted only at the sender boundary; invalid signed IDs are excluded, never cast
into large valid IDs. Internal class -1 maps to PC Unknown (255); class values
outside [-1,255] cannot be represented and that record is excluded. Class IDs
80..254 are preserved rather than changed because the PC lacks a name mapping.
Invalid geometry/confidence records are excluded individually; valid boxes are
clipped to original 640×480 bounds. The internal tracker output is unchanged.

The PC assumes **original 640×480 pixels**. Video and metadata have no resize or
coordinate conversion on the sender; incompatible source dimensions are
rejected. Width/height, version, message type and detection count are not sent.
The original capture ID is preserved, while the RTP timestamp is computed from
the video submission index and configured FPS at 90 kHz. Gaps in capture IDs do
not change that mapping. `gui_started_us` alone does not measure network latency
without a clock synchronization contract.

A datagram carries display records for one frame, including a header for zero
records. At the default 1200-byte limit, 46 tracks fit (1178 bytes); 47 would need
1203 bytes. Capacity is derived from the configured payload limit, not a fixed
track count. Valid records are included in stable input order up to capacity;
extra display records are excluded, and trackCount is the actual encoded count.
The local metadata object supports a selected-target priority hint, which is not
transmitted. When a valid selected target is supplied it is included first, then
the other records in stable order. Current deferred control supplies no target
hint because input-ID following is unimplemented. Display exclusion does not
mean TargetLost or affect tracking/control state. No fragmentation is added.

The adopted overflow rule is in Application 설계 §2.5; the roadmap's §§5/7 still
list that policy as unresolved. This change follows the explicitly adopted design
for selection; the numeric UDP limit remains a provisional, unmeasured setting.
Actual UDP send errors still propagate as sender failure; transient transport
error recovery is not added here.

## Control TCP 5002

Header: `LWIR:uint32, category:uint8, command:uint8, payload_length:uint16`.
There is no version field. Reads may split or combine messages arbitrarily.
Unsupported category/command/length or a board-only message from the PC closes
the client; the listener remains available for reconnection.

| Category | Command | Payload | Deferred strategy behavior |
|---|---|---|---|
| System (1) | PING (4) | empty | ACK OK |
| Mode (2) | AUTO (1) | Track ID uint32 | ACK NOT_READY; zero ID gets INVALID_TARGET |
| Mode (2) | MANUAL (2) | empty | ACK OK, already Manual with no target |
| Gimbal (3) | MOVE (1) | signed x,y bytes (-1..1) | No operation; no ACK per PC contract |
| Gimbal (3) | HOME (3) | empty | ACK NOT_READY |

On every connection the deferred handler sends STATUS (System command 3):
`Manual / None / Idle / error 0 / selected ID 0`. No tracking or motion is active.
The state payload is `mode:uint8, target_state:uint8, gimbal_state:uint8,
error:uint8, selected_ID:uint32` (8 bytes). ACK (System command 1) contains the
original category, original command and result (3 bytes).

The codec also supports INIT (System command 2), with the same state payload.
The deferred handler has no reinitialization event or state transition to report;
it does not invent periodic STATUS, AUTO success or HOME completion.

**Input-ID target following and gimbal control remain unimplemented.** Received
AUTO IDs retain all 32 bits, but are not forwarded to `TargetSelector` or
`ControlThread`. The existing ByteTrack detector-output tracking is unchanged.
A future handler must own real state transitions/target validation and integrate
actual control readiness, disconnect and heartbeat policies. The deferred
handler has no moving state and therefore implements no motion watchdog.

## Verification scope

Tests use PC source fixtures for exact metadata and control bytes, TCP loopback
for split/coalesced messages, reconnect and stop, and CPU software encoders for
RTP timestamp matching. Test frames and track records are explicit transport
fixtures, not inference results. These checks do not establish Windows GUI
end-to-end behavior or board hardware performance.

DXNN loading, inference, tensor validation, NPU benchmarks and board MPP/gimbal
execution remain unverified without the actual hardware.
