# Runtime–PC communication

Runtime follows `pc-application`'s base video wire contract, the optional fixed
telemetry extension in `feat/runtime-telemetry`, and the
adopted decisions in [Application 설계](https://app.notion.com/p/3f29de3f14a280829f90dc2dfd666d04).
The [1103 roadmap](https://app.notion.com/p/40c9de3f14a28074905bd2239d6e9fdb)
describes wider state/control/gimbal work. The [tracking handler](auto_control.md)
connects AUTO(ID) and keyboard MOVE to one Waveshare Serial executor, independently
of tracking frame arrival for manual motion and watchdog checks. The default
`deferred` handler remains communication-only; HOME completion is unimplemented.
The wire contract has no version field or YAML protocol selector.
The PC Python LAN client uses this contract; the existing Qt client remains in
the PC repository.

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
    host: <PC IPv4>
    port: 5001
    max_packet_bytes: 1200
    telemetry:
      enabled: true
  command:
    enabled: true
    handler: deferred
    bind_address: 0.0.0.0
    port: 5002
    receive_timeout_ms: 1000
```

This is an excerpt; retain the other required keys in `config/runtime.yaml`.
On the PC, set `control.host` to the board IP and select the same video codec.
Set GUI enablement and destination/listener addresses in `config/runtime.yaml`
for the deployment.
JPEG remains selectable with `codec: jpeg` / `encoder: mppjpegenc`; Docker CPU
transport tests explicitly select `x264enc` or `jpegenc`. No automatic encoder
fallback is performed.

`IGuiProtocol` separates wire encoding from transport. The single supported
implementation, `PcGuiProtocol`, is constructed directly; no protocol enum,
factory or YAML selector is needed. `GuiCommandHandlerFactory` creates the
`IGuiCommandHandler` application strategy selected by `command.handler`.
The `tracking` handler adds AUTO(ID) following through a shared executor; see [AUTO control](auto_control.md).
Unknown handler settings fail during configuration; an omitted handler defaults
to `deferred`; `tracking` requires control to be enabled and handles both AUTO
and manual commands. The removed `manual` setting is rejected during configuration.

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
records. With telemetry disabled, the default 1200-byte limit fits 46 tracks
(1178 bytes); 47 would need 1203 bytes. Telemetry is enabled by default and
reserves 124 bytes, fitting 41 tracks (1177 bytes with the trailer). That
reservation also applies before a completed sample is available. Capacity is
derived from the configured payload limit, not a fixed
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

### Optional Runtime telemetry

`gui.metadata.telemetry.enabled` defaults to **true**, including when omitted.
It requires a PC application that supports the fixed LWTM extension; the older
PC parser rejects packets containing a trailer. Set it to false for an older PC.
The new decoder accepts both the base packet and exactly 124 extra bytes.
There is no version, flags, variable trailer length or protocol selector.

The trailer belongs to a completed **earlier** frame K, where K < current frame N;
it never changes the current boxes or video association. See
[Runtime telemetry](runtime_telemetry.md) for fields, timing meanings and collection.
The minimum configured packet limit with telemetry enabled is 152 bytes.

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
| Gimbal (3) | POSITION (4) | empty | Gimbal (3), `0x84`: valid:u8, pan:f32, tilt:f32 (big endian, display estimate 0..100 %) |

On every connection the deferred handler sends STATUS (System command 3):
`Manual / None / Idle / error 0 / selected ID 0`. No tracking or motion is active.
The state payload is `mode:uint8, target_state:uint8, gimbal_state:uint8,
error:uint8, selected_ID:uint32` (8 bytes). ACK (System command 1) contains the
original category, original command and result (3 bytes).

The codec also supports INIT (System command 2), with the same state payload.
The deferred handler has no reinitialization event or state transition to report;
it does not invent periodic STATUS, AUTO success or HOME completion.

**The default deferred strategy does not follow targets or complete HOME.**
Select the [tracking handler](auto_control.md) for AUTO(ID) following and manual override.
It preserves the wire format and uses a single device executor; HOME remains NOT_READY.
The behavior in the deferred table applies only to `handler: deferred`. Received
AUTO IDs retain all 32 bits in both strategies. The tracking handler validates IDs
against fresh tracker output and owns real transitions through the shared executor;
see [AUTO control](auto_control.md) for readiness, STATUS, disconnect, and heartbeat
rules. The deferred handler has no moving state and therefore implements no motion
watchdog.

## Pipeline extension — TCP 5002

This runtime adds category **Pipeline (4)** to the existing 8-byte header.
PC software must implement this extension; older PC applications remain usable
with the existing commands. All integers below are unsigned and big endian.
Per-item names, installed-file status and active IDs are also transmitted;
model paths and tracker configuration records stay on the board. AppConfig requires both catalogs to use
consecutive IDs `1..count`. Request IDs retain all 32 bits, including zero.

| Direction | Command | Payload |
|---|---|---|
| PC → board | Model count `0x01` | request_id:u32 (4 bytes) |
| PC → board | Tracker count `0x02` | request_id:u32 (4 bytes) |
| PC → board | Change model `0x03` | request_id:u32, model_id:u32 (8 bytes) |
| PC → board | Change tracker `0x04` | request_id:u32, tracker_id:u32 (8 bytes) |
| PC → board | Model info `0x05` | request_id:u32, model_id:u32 (8 bytes) |
| PC → board | Tracker info `0x06` | request_id:u32, tracker_id:u32 (8 bytes) |
| PC → board | Active tracker policy `0x07` | request_id:u32 (4 bytes) |
| PC → board | Change active tracker policy `0x08` | request_id:u32, field_mask:u32, warmth_min:u32, warmth_max:u32, edge_birth:f32, confirmation_ms:u32, flags:u32 (28 bytes) |
| Board → PC | Model count `0x81` | request_id:u32, count:u32 (8 bytes) |
| Board → PC | Tracker count `0x82` | request_id:u32, count:u32 (8 bytes) |
| Board → PC | Model result `0x83` | request_id:u32, phase:u8, result:u8 (6 bytes) |
| Board → PC | Tracker result `0x84` | request_id:u32, phase:u8, result:u8 (6 bytes) |
| Board → PC | Model info `0x85` | request_id:u32, model_id:u32, flags:u32, UTF-8 name[64] (76 bytes) |
| Board → PC | Tracker info `0x86` | request_id:u32, tracker_id:u32, flags:u32, UTF-8 name[64] (76 bytes) |
| Board → PC | Active tracker policy `0x87` | request_id:u32, tracker_id:u32, warmth_min:u32, warmth_max:u32, edge_birth:f32, confirmation_frames:u32, confirmation_ms:u32, flags:u32 (32 bytes) |
| Board → PC | Policy change result `0x88` | request_id:u32, phase:u8, result:u8 (6 bytes) |

Info flags bit 0 means the referenced DXNN file is installed; bit 1 means the ID
is active. Names are NUL-terminated within 64 bytes, with zero padding. An
unknown catalog ID returns ID 0 and an empty name. The installed bit does not
claim DXRT load or inference success; a change result confirms activation.
Policy flags are bit 0 class gate, bit 1 GMC, and bit 2 post-tracking ReID.
The policy reply is read-only and describes the active tracker's YAML settings,
except `confirmation_frames`, which is the currently effective count after
`confirmation_ms` adapts to the observed frame rate;
tracker ID 0 and zeroed fields mean no active tracker. It does not report
the measured frame rate or imply that filters can be changed by this command.
An older Runtime may reject `0x07` and close the connection.

For `0x08`, field mask bits 0..5 select warmth minimum, warmth maximum, edge
birth, confirmation time, GMC, and class gate respectively. Flag bits 0 and 1
carry the requested GMC and class gate values. Unselected fields are ignored.
The mask must be nonzero and all values use the same ranges as the YAML loader.
Result `7=InvalidConfiguration` reports a rejected mask or value. A successful
request creates a replacement tracker through `TrackerChangeService` and
`TrackerManager`, increments the epoch, and resets track/Gallery state. Candidate
creation failure keeps the previous tracker and policy. The configured YAML
catalog entry is unchanged; changing to another tracker or restarting Runtime
returns to that entry's YAML policy. The accepted/completed response sequence
matches model and tracker changes. An older Runtime may reject `0x08`.

Phase is `1=Accepted`, `2=Completed`. Results are `0=OK`, `1=InvalidId`,
`2=Busy`, `3=CreationFailed`, `4=Restored` (previous model),
`5=RestoreFailed`, `6=NotReady` (startup/shutdown or unavailable request service).
These results are separate from the existing System ACK result enum. Pipeline
commands do not also emit System ACK. Rejected requests receive Completed only;
accepted requests receive Accepted/OK followed by their actual Completed result.
An already-active ID succeeds without recreating the instance and follows the
same two-response contract. A tracker creation failure preserves its previous
instance. A failed model load reports Restored only after rollback succeeds.

For example, request 10 changes to model 2:

```text
PC:       4c574952 04 03 0008 0000000a 00000002
Accepted: 4c574952 04 83 0006 0000000a 01 00
Complete: 4c574952 04 83 0006 0000000a 02 00
```

`PipelineCommandHandler` wraps the configured deferred/tracking handler and
associates with `IPipelineRequests`, implemented by the cpp-private runtime.
The TCP receiver worker validates and dispatches requests, sends acceptance,
and polls completion futures at most every 25 ms while continuing PING and
partial-packet reception. The model manager performs model teardown/load/rollback;
the runtime loop performs tracker create/swap. These workers never write the TCP
socket; the receiver worker sends every control reply. Video/metadata sender
workers keep their existing responsibilities and the wire metadata is unchanged.

At most one model change and one tracker change may be outstanding globally.
The handler additionally limits a session to two pending changes and rejects a
duplicate pending change request_id as Busy. Clients should not reuse outstanding
IDs. Disconnect drops that session's response subscriptions but does not cancel
an accepted operation. Its result is never delivered to a replacement session;
a reconnecting client may query counts again and receives Busy until the actual
outstanding operation completes. Count queries and PING do not wait for switching.
Without an associated runtime request service, count queries return zero and
switches return NotReady. Board-only Pipeline responses sent by a PC, unsupported
commands, and incorrect declared payload lengths disconnect that client as before.

## 원본 녹화 명령 — TCP 5002

카테고리 `Recording (5)`는 단일 LAN PC의 녹화 제어에 사용한다. 모든 숫자는
big endian이다. 요청 `1`(상태)과 `3`(종료)은 `request_id:u32`(4바이트),
요청 `2`(시작)는 `request_id:u32, video:u8=1, name:utf8[64]`(69바이트)다.
응답은 요청 코드에 `0x80`을 더하고 `request_id:u32, result:u8,
active:u8, failed:u8, reserved:u8=0, session:u32, frames:u64, dropped:u64`
(28바이트)를 돌려준다. `result=0`은 요청 수락이다.

카메라 원본 `CV_16UC1` 프레임은 검출 큐에서 제외되기 전에 별도 120장
유한 큐로 전달된다. 저장 스레드가 밀리면 `dropped`를 증가시키며 추론
경로를 막지 않는다. 출력은 `measurement.output_root/recordings/MEASURE_*/`
아래 `frames.raw.gz`(640×480, uint16 little endian 연속 프레임),
`frames.csv`(저장 순번·카메라 frame ID·steady clock ns),
`tracks.csv`(게시된 추적 결과의 frame ID·Track·박스), `recording.txt`다.
`tracks.csv`에는 저장 큐가 놓친 프레임의 결과가 있을 수 있어 `frames.csv`의
frame ID와 결합해야 한다. 종료 시점에 아직 처리 중인 마지막 프레임의 Track은
포함되지 않을 수 있다. PC 페이지는 로컬 브리지에서 시작·종료·상태를 조회한다.

완료된 녹화는 기존 TCP 5002 연결로 PC에 복사할 수 있다. 요청 `4`는
`request_id:u32`, 응답 `0x84`는 `request_id:u32, result:u8,
directory:ascii[64]`다. `result=0`일 때 최신 완료 디렉터리 이름을 돌려준다.
요청 `5`는 `request_id:u32, directory:ascii[64], file_id:u8,
offset:u64, limit:u16`(79바이트)이며 `limit`은 1..32768이다. 파일 ID
1..4는 위에 나열한 네 파일 순서다. 응답 `0x85`는 `request_id:u32,
result:u8, total:u64, offset:u64, bytes[0..32768]`다. 녹화 중인 디렉터리는
읽을 수 없고, 디렉터리 이름과 파일 ID는 Runtime에서 제한한다. PC GUI는
네 파일을 `/workspace/recordings/MEASURE_*/`에 임시 디렉터리로 받은 뒤
gzip 무결성을 검사하고 완료 디렉터리로 바꾼다. 별도 HTTP 서버, SSH,
Tailscale은 이 전송에 사용하지 않는다.

## Verification scope

Tests use PC source fixtures for exact metadata and control bytes, TCP loopback
for split/coalesced messages, reconnect and stop, and CPU software encoders for
RTP timestamp matching. Test frames and track records are explicit transport
fixtures, not inference results. These checks do not establish Windows GUI
end-to-end behavior or board hardware performance.

DXNN loading, inference, tensor validation, NPU benchmarks and board MPP/gimbal
execution require actual hardware verification.
