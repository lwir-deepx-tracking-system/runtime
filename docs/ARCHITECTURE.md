# 현재 Runtime 아키텍처와 설계 결정

확인 기준: `feat/python-runtime-integration`, 기준 `6238978` 및 미커밋 변경 (2026-11-11).
이 문서는 구현 상태를 설명하며 Python 참고 ZIP이나 PC Qt 앱의 기능을
검증한 문서가 아니다. 변경 전에는 [`PROJECT_PROGRESS.md`](PROJECT_PROGRESS.md)의
Git 상태와 대조한다.

## 프레임 처리와 Queue

```text
CameraThread --FrameMessage--> DetectionThread --DetectionResult--> TrackingThread
                                                             |                 |
                                                             |                 +--TrackingResultPtr--> ControlThread
                                                             +--bypass--------> GuiSenderThread
                                                                               ^
TrackingThread ------------------------TrackingResultPtr-----------------------+
```

| 경계 | 생산자와 소비자 | 현재 계약 |
| --- | --- | --- |
| Camera → Detection | `CameraThread` → `DetectionThread` | 원본 `CV_16UC1` 영상의 `FrameContextPtr` 공유. 입력 지연 시 최신 2개 유지 |
| Detection → Tracking | `DetectionThread` → `TrackingThread` | 비동기 완료를 제출 순서대로 재정렬한 `DetectionResult`. 프레임과 `model_generation` 포함 |
| Tracking → Control | `TrackingThread` → `ControlThread` | `TrackingResultPtr` 공유, 최신 1개 유지 |
| Tracking → GUI | `TrackingThread` → `GuiSenderThread` | 같은 결과 객체 공유, 최신 2개 유지. 모델 교체 중에는 DetectionThread가 영상 bypass 결과를 보낼 수 있음 |

`CameraThread`가 프레임 ID와 촬영 시각을 부여한다.
`DetectionThread`는 `submit_async()` 완료를 `completed_results_`에 보관하고
`next_output_sequence_` 순서로 결과 큐에 전달한다. 원본 프레임은
`FrameContextPtr`로 공유하며 별도 영상 복사를 만들지 않는다.
결과 좌표는 원본 프레임 픽셀 좌표다.

`RuntimeWorkers`는 ModelManager 초기화 → TrackerChangeService → GUI 수신·송신
→ Control → Tracking → Detection → Camera 순서로 시작한다. 종료 시 중단을
요청하고 카메라·Detection을 합류시킨 뒤 나머지 소비자와 GUI/Control을 정리한다.
`DetectionThread`가 자신이 생산하는 Detection 결과 큐를 닫는다.

## Factory, Manager, 설정

- `AppConfig`가 `config/runtime.yaml`과 참조된 Detection·Tracking·ReID YAML을
  typed config로 읽고 검증한다. Processing 객체는 YAML을 직접 읽지 않는다.
- `DetectorFactory`/`ReidFactory`가 각 모델 구현을 생성한다.
- `TrackerFactory::create_configured()`는 선택된 Tracker와 필요한 ReID
  Pipeline·Matcher를 한 객체 수명에 결합한다. ByteTrack 단독은 RepVGG를
  생성하지 않는다.
- `TrackerManager`는 카탈로그, 활성 `shared_ptr<Tracker>`, ID와 epoch를
  관리한다. 후보를 먼저 생성하고 성공 시 교체한다. 처리 중인 snapshot이
  있으면 이전 객체의 소멸은 마지막 참조가 끝날 때까지 지연된다.
- `TrackerChangeService`는 요청 유효성, Busy 상태와 완료 future를 관리하며
  Manager의 `activate()`를 호출한다.

현재 Runtime 카탈로그는 ID 1 ByteTrack, ID 2 BoT-SORT, ID 3 ByteTrack+
Post-ReID다. `tracking.id`가 초기 모드를 선택한다. Benchmark의 기존
`reid.enabled: true`는 ID 1에서도 사후 ReID를 켜는 호환 동작이다.
세부 설정과 출력 파일 의미는 [`multitracker.md`](multitracker.md)에 있다.

## Tracker 실행 순서와 세대

`TrackingThread`는 `TrackerManager::snapshot()`을 얻은 뒤
`tracker->process(DetectionResult)`를 호출한다. 종류별 분기는 없다.
기존 직접 호출용 `track(vector<Detection>)`도 남아 있다.

| 구성 | 실행 순서 | 상태 소유 |
| --- | --- | --- |
| ByteTrack 단독 | Detection → ByteTrack | Kalman/Track 상태 |
| ByteTrack + Post-ReID | Detection → ByteTrack → RepVGG → ReidMatcher | ByteTrack 상태와 Stable ID Gallery |
| BoT-SORT | Detection → RepVGG → BoT-SORT Association | Kalman/Track 상태와 Track별 Appearance 특징 |

BoT-SORT 임베딩은 Detection 인덱스에, 사후 ReID 임베딩은 원본 Track ID에
연결된다. BoT-SORT의 Crop 상한 0은 조건에 맞는 Detection 전체를 뜻하며,
특징이 없으면 해당 후보는 IoU 경로를 사용한다. 선택적 `FramePolicyTracker`가 sparse optical flow GMC를 계산해 공통 hook으로 공급한다.
BoT-SORT/ByteTrack이 Kalman 상태에 적용하고 PostReid 조합은 내부 Tracker로 전달한다.
GMC history는 Tracker reset/교체 시 함께 초기화한다.

`TrackingThread`는 Detection 결과의 `model_generation`이 현재 세대와 다르면
버린다. 동일 Tracker epoch에서 모델 세대가 바뀌면 Tracker의 `reset()`을
호출해 Kalman/Appearance/Gallery를 함께 초기화한다. Tracker 교체 시 새
객체의 epoch를 기록하고, 작업 완료 후에도 epoch·모델 세대를 다시 검사한다.
`publish_if_current()` 잠금 안에서 결과를 게시해 이전 Tracker의 완료
결과가 새 Tracker 결과로 게시되지 않게 한다.
`GuiSenderThread`와 `GimbalCommandExecutor`도 세대를 확인한다.

## 모델 교체, 제어, 네트워크

`ModelManagerThread`와 `ActivePipelineSlot`이 Detection 모델 수명을 관리한다.
전환 시 Detection 작업과 콜백을 합류시킨 뒤 파이프라인을 교체하거나
이전 모델 복구 결과를 보고한다. 교체 중 GUI 영상 bypass 경로가 있다.
`RuntimeControl`은 실행 상태, `generation`, `tracker_epoch`, 중단 요청을
공유한다.

`ControlThread`는 Tracking 결과를 `GimbalCommandExecutor`에 전달한다.
기존 Target Selection, AUTO/MANUAL, STOP 안전 동작과 단일 짐벌 명령
경로를 유지한다. Tracker/모델 세대가 달라진 이전 목표 결과는 제어에서
사용하지 않는다. 실장비 움직임은 이 문서 작성 과정에서 시험하지 않았다.

현재 `config/runtime.yaml`은 H.264/RTP 영상 UDP 5000, Tracking Metadata
UDP 5001, 명령·응답 TCP 5002를 설정한다. 프로토콜의 실제 바이트 계약은
[`pc_communication.md`](pc_communication.md)에 있다. Python HTTP/MJPEG
서버는 현재 Runtime 경로에 없다. PC Qt 앱 저장소는 인접 `pc-application`에 있으며 분석만 수행했다.
PC 측 실제 연결·표시 검증 및 UI 확장은 아직 진행하지 않았다.

## 측정과 검증 경계

`StageMetric`, `DetectionTiming`, `IMeasurementSink`, `MetricsWriter`가
기존 Runtime 계측을 담당한다. Benchmark는 Detection, Tracking/Association,
ReID 추론, 사후 Matching, Crop 수, 프레임 전체 시간을 기록한다. 이 문서의
구조 설명은 CPU 테스트와 소스 확인에 근거한다. DXRT 모델 로딩·추론,
실제 NPU 지연, PC GUI 연동, 데이터셋 추적 지표는 별도 실기 검증이 필요하다.

## 선택적 프레임 정책 조합

`TrackerFactory::create_configured()`는 필터/GMC/Edge 설정이 있을 때만
`FramePolicyTracker`로 기존 Tracker 구성 객체를 감싼다. TrackingThread와
Benchmark는 동일한 `process()`를 사용한다. Warmth는 Association 이전 Detection
목록에 적용하고 Class/Edge/confirmation은 Track의 매칭 및 생성 시점에 적용한다.
필터는 원본 이미지를 복사하지 않으며 Detection 인덱스를 Embedding 및 BoT-SORT
진단에 다시 매핑한다. PostReID의 Track/Stable ID는 Detection 인덱스로 변환하지 않는다.
기존 설정 기본값은 유지한다. 고정 범위 GMC 영상 매핑은 Python percentile/EMA
매핑과 다르며 동일 성능 검증은 아직 없다.

## YAML 리소스 경로

`path_base`가 있으면 해당 YAML 파일의 디렉터리를 기준으로 base를 계산하고
모델 `path`, Detection/ReID config, matcher config, Detection/Tracker catalog
config 경로를 절대경로로 정규화한다. 파일 존재 여부나 Tensor 검증은 우회하지 않는다.
Runtime/Benchmark는 `path_base: ..`, 하위 모델 YAML은 `path_base: ../..`로
기존 프로젝트 루트 기준 파일을 가리킨다. 절대경로는 그대로 유지한다.
외부의 과거 YAML이 `path_base`를 생략하면 이전 CWD 기준 의미를 유지한다.
설정 파일을 다른 위치에 복사할 때 base도 명시적으로 조정해야 한다.
측정 결과 및 데이터셋 경로의 기존 해석 방식은 이번 변경 대상이 아니다.
