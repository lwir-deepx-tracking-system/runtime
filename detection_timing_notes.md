# Detection timing 변경 기록

작성일: 2026-10-13

이 문서는 Runtime과 Benchmark의 Detection 실행 방식 및 시간 측정 기준을 임시로 기록한다.

## 1. 확정된 실행 방식

- Runtime과 Benchmark는 모두 `DxAppDetectionPipeline::submit_async()`를 실행한다.
- Benchmark 전용 동기 `detect()` 경로는 사용하지 않는다.
- 전처리, DXRT 비동기 추론, 후처리는 하나의 Detection pipeline에서 처리한다.
- 각 비동기 요청이 `DetectionTiming`, frame ID, DXRT job ID를 함께 소유한다.
- callback 완료 순서가 제출 순서와 달라도 job ID와 frame ID가 섞이지 않는다.

```text
FrameContext
  → 전처리
  → RunAsync() 제출 및 job ID 저장
  → DXRT callback에서 후처리
  → Wait(job ID)
  → GetJobMetrics(job ID)
  → Completion에 DetectionTiming 전달
```

DXRT의 job별 profiler 값은 callback 반환 후 완성되므로 별도 completion worker가
`Wait(job_id)` 이후 `GetJobMetrics(job_id)`를 호출한다.

## 2. 시간 항목 정의

### `preprocess_ms`

```text
전처리 시작
  → 16-bit LWIR 영상을 8-bit로 변환
  → 3채널 변환
  → letterbox resize
  → 모델 입력 tensor 변환 및 입력 buffer 복사 완료
```

### `latency_ms`

```text
RunAsync() 호출 직전
  → DXRT 내부 대기 및 처리
  → DXRT completion callback 진입
```

순수 NPU 연산 시간만 의미하지 않는다. DXRT queue 대기, buffer 대기, 입력·출력 처리,
전송 및 callback 전달 지연 등이 포함될 수 있는 애플리케이션 관점의 비동기 왕복 시간이다.

### `npu_processing_ms`

해당 job의 DXRT profiler에서 `inference_core_all_us`를 읽어 밀리초로 변환한 값이다.
CPU fallback이나 전체 비동기 대기시간이 아니라 순수 NPU core 실행시간을 나타낸다.

`npu_processing_ms`는 `latency_ms`에 포함되는 구간이므로 두 값을 합산하면 안 된다.

### `postprocess_ms`

```text
DXRT callback 진입
  → output tensor 해석
  → confidence filtering 및 NMS
  → 원본 frame 좌표로 Detection 변환 완료
```

## 3. CSV 형식

Runtime의 `detection_timing.csv`:

```text
frame_id,preprocess_ms,latency_ms,npu_processing_ms,postprocess_ms
```

Benchmark의 sequence별 timing CSV:

```text
frame_id,preprocess_ms,latency_ms,npu_processing_ms,postprocess_ms,tracking_ms
```

## 4. Runtime과 Benchmark 차이

### Runtime

```yaml
detection:
  max_inflight: 4
  buffer_count: 6
  use_ort: false
```

카메라 프레임이 연속으로 들어오므로 최대 네 개의 Detection 요청이 동시에 진행될 수 있다.
`max_inflight` 값이 실제 처리량과 FPS에 영향을 줄 수 있다.

### Benchmark

```yaml
detection:
  max_inflight: 1
  buffer_count: 6
  use_ort: false
```

현재 Benchmark는 프레임 하나를 `submit_async()`한 뒤 해당 completion future가 끝날 때까지
기다리고 다음 프레임을 제출한다. 따라서 순차 latency 측정 방식이며 실제 동시 요청 수는
항상 하나다.

Benchmark에서 `max_inflight`만 4로 바꿔도 여러 요청을 동시에 제출하지 않으므로 성능은
달라지지 않는다. 최대 처리량을 측정하려면 여러 프레임을 먼저 제출하고 completion을 나중에
회수하는 별도의 throughput 측정 방식이 필요하다.

## 5. ORT 설정

현재 `InferenceOption`은 AppConfig가 검증한 YAML 값을 사용한다.

```cpp
dxrt::InferenceOption option;
option.bufferCount = config.buffer_count;
option.useORT = config.use_ort;
```

- ONNX Runtime CPU fallback을 사용하지 않는다.
- 모델의 NPU task만 실행한다.
- Runtime과 Benchmark가 동일한 설정을 사용한다.
- 모델에 필수 CPU task가 포함된 경우에는 실행 실패 여부를 장비에서 확인해야 한다.

## 6. `max_inflight`와 DXRT `bufferCount`의 차이

- `max_inflight`: 애플리케이션이 동시에 진행하도록 허용하는 Detection 요청 수
- `bufferCount`: DXRT engine 내부의 입력·출력 buffer pool 개수

두 값은 서로 다른 설정이다. 현재 Runtime과 Benchmark YAML은 `buffer_count: 6`을 사용한다.
`buffer_count: 0`으로 바꾸면 DXRT 자동 설정을 사용할 수 있다. Runtime의
`max_inflight: 4`가 DXRT `bufferCount: 4`를 의미하지는 않는다.

## 7. 비교용 장비 측정 기록

다음 값은 개발 환경에서 재현한 결과가 아니라 대상 장비에서 사용자가 확인한 참고값이다.

| 실행 조건 | NPU processing | Latency | FPS |
| --- | ---: | ---: | ---: |
| `dxrun -s -v`, ORT 비활성 | 약 3.8 ms | 약 27 ms | 약 30 |
| ORT 활성 | 약 3.8 ms | 약 42 ms | 약 22.4 |
| 비동기 실행, ORT 활성, `buffer-count=1` | 약 3.8 ms | 약 50~60 ms | - |

NPU processing이 약 3.8 ms로 유지되면서 latency만 증가한 것은 NPU core 연산 자체보다
DXRT/CPU/queue/buffer/전송 구간이 전체 왕복 시간을 늘렸다는 의미로 해석한다.

## 8. 요구 버전과 검증 상태

- job별 `GetJobMetrics()`를 사용하므로 DXRT 3.4.0 이상이 필요하다.
- Runtime 빌드 성공
- Benchmark 빌드 성공
- Detection 관련 CPU 테스트 통과
- 현재 개발 환경에는 NPU가 없어 실제 DXNN 로딩, 추론, tensor 및 NPU timing 값은 미검증이다.

대상 장비에서는 다음을 확인한다.

1. Runtime과 Benchmark가 `useORT=false`로 정상 실행되는지 확인한다.
2. `npu_processing_ms`가 0이 아니며 `dxrun`의 약 3.8 ms와 유사한지 확인한다.
3. 일반적으로 `latency_ms >= npu_processing_ms`인지 확인한다.
4. Runtime에서 `max_inflight: 1`과 `4`의 FPS 및 latency 분포를 비교한다.
5. profiler 활성화로 인한 성능 차이가 있는지 동일 조건으로 비교한다.
