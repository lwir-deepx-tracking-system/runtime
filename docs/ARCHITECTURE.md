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
Post-ReID, ID 4 필터·GMC + Post-ReID, ID 5 OC-SORT형 설정, ID 6 SORT형
설정이다. `tracking.id`가 초기 모드를 선택하며 Runtime 기본값은 ID 1이다.
기존 `TrackerChangeService`가 이 카탈로그의 새 ID도 교체한다. Benchmark의 기존
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
서버는 현재 Runtime 경로에 없다. PC Qt 앱 저장소는 인접 `pc-application`에 있으며
`feat/python-runtime-integration`에서 기존 TCP Tracker 변경과 HOME 버튼이
연결됐다. PC는 TrackerCount로 ID 개수만 조회하며, 현재 프로토콜은 이름과
활성 ID를 제공하지 않아 접속 직후 현재 Tracker를 Unknown으로 표시한다.
실제 PC↔Orange Pi LAN 연결·표시 검증은 아직 수행하지 않았다.

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

## 단일 LAN 호스트 GUI와 카탈로그 정보 (2026-11-12)

기존 UDP 영상 5000·메타데이터 5001과 TCP 명령 5002를 유지한다. Qt GUI는
직접 LAN으로 연결된 한 호스트 PC에서 실행한다. Python HTTP/MJPEG 서버를
추가하지 않는다. TCP Pipeline 카테고리에 ModelInfo/TrackerInfo 요청 5/6,
응답 0x85/0x86을 추가했다. Runtime YAML의 표시 이름, DXNN 파일 설치 여부,
Manager의 현재 ID를 전달한다. 설치 여부는 DXRT 추론 성공과 구분한다.
기존 명령 1..4, 스레드·Queue와 모델·Tracker 변경 정책은 그대로다.
바이트 계약은 [`pc_communication.md`](pc_communication.md)에 기록한다.

## 원본 Python 화면과 추가 Track 확인 정책 (2026-11-12)

사용자 요청에 따라 최종 PC 화면은 `model-bank/tracking_server/page/`의
HTML/CSS/JS 디자인·동작을 기준으로 한다. 이전 Qt 위젯 재구현은 최종 화면
계약이 아니다. 화면의 HTTP 호출과 `/stream` 입력은 PC 데스크톱 프로세스의
LAN 브리지로 대체하며, Runtime에는 Python 웹서버를 두지 않는다. 현재
PC 브리지는 아직 구현되지 않았다. 기능별 이식 경계는
[`MODEL_BANK_DELTA_20261112.md`](MODEL_BANK_DELTA_20261112.md)에 기록한다.

선택형 `confirmation_ms`는 `AppConfig`가 0~1000 ms로 검증한다. 값이 0이면
기존 `confirmation_frames` 정책을 그대로 쓴다. 켜진 Tracker는 Factory가
`FramePolicyTracker`로 감싸며, 이 객체가 처리 프레임의 촬영 시각 간격
최근 50개의 중앙값으로 FPS를 계산한다. 간격 1초 초과는 끊김으로 보고
제외한다. 시작은 25 FPS 기준이고, 0.15장 hysteresis와 기존
`confirmation_frames` 하한을 적용한다. 공통 `set_confirmation_frames()`가
ByteTrack·BoT-SORT와 Post-ReID 내부 Tracker에 값을 전달한다.
Tracker reset/교체 시 시간 창도 초기화된다. 기본 Runtime Tracker에는
이 옵션을 설정하지 않았고 Python 참고 필터 프리셋에서만 130 ms를 켰다.

Pipeline TCP 명령 `0x07`은 활성 Tracker의 YAML 정책을 읽기 전용으로
반환한다. 모델·Tracker 변경에는 기존 Manager와 완료 응답 경로를 사용한다.
`0x08` 정책 변경은 `TrackerChangeService`가 후보 Tracker를 생성한 뒤
`TrackerManager`에 commit한다. 성공 시 기존 Track/Kalman/Gallery는
초기화되고 epoch가 증가한다. 실패 시 이전 인스턴스와 YAML 설정을 유지한다.
설정값은 현재 실행의 활성 Tracker에만 적용되며 다른 Tracker 선택 또는
Runtime 재시작 시 카탈로그 YAML로 돌아간다. `0x07`의 확인 장수는
현재 프레임 속도 보정을 거친 값을 원자적으로 읽는다.
PC의 Python 데스크톱 프로세스는 원본 model-bank 페이지를 WebKit에 로드하고
기존 UDP H.264/RTP·메타데이터 및 TCP 명령에 직접 연결한다. 화면의 아직
지원하지 않는 조작은 실패 응답을 보낸다. 현재 영상 오버레이는 PC에 도착한
최신 메타데이터를 쓰며 RTP timestamp에 의한 프레임 동기화는 남아 있다.
