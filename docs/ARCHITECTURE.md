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
Post-ReID, ID 4 필터·GMC + Post-ReID, ID 5 OC-SORT XYWH 변형, ID 6 SORT,
ID 7 독립 OC-SORT 구현이다. ID 5는 `ocsort_xywh.yaml`, ID 7은 `ocsort.yaml`을 사용한다.
기존 `type: ocsort`와 ByteTrack 키를 쓰는 외부 설정은 XYWH 변형으로 해석하고,
독립 OC-SORT 키와의 혼합은 오류로 처리한다. `tracking.id`가 초기 모드를 선택하며 Runtime 기본값은 ID 1이다.
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

## 원본 녹화 경로 (2026-11-16, 호스트 구현)

`CameraThread`가 받은 640×480 `CV_16UC1` 프레임을 Detection 큐에 전달하기 전에
`RawRecorder`의 최대 120장 큐에도 공유 포인터로 넣는다. 저장 스레드는
`frames.raw.gz`와 `frames.csv`, `tracks.csv`를 기록한다. 큐가 차면 녹화만 프레임을 놓치고
그 수를 LAN 상태로 보낸다. Tracker·Detection·Control 결과와 프레임 정책은
녹화 경로에 의해 바뀌지 않는다. 단일 PC는 기존 TCP 5002의 카테고리 5로
시작·종료·상태를 제어한다. 재생/파일 전송/측정 세션은 아직 연결되지 않았다.

## Encoder CPU 및 telemetry 통합

`FrameTelemetryCollector`는 측정 sink를 감싸 CSV 기록을 전달하고 프레임 단계·RTP
완료 이벤트를 유한 큐에 수집한다. GUI telemetry가 활성화되면 CSV 저장이 꺼져도
측정 시각을 수집한다. `GuiSender`가 집계한 telemetry를 metadata trailer로 전송한다.
녹화는 기존 `RawRecorder` 경로를 사용한다.

`--encoder-cpu`는 H.264 GUI 송신이 켜진 실행에서 첫 RTP 출력 뒤 10초 워밍업,
60초 프로세스 CPU·송신 FPS 계측을 추가한다. SW/HW 비교 설정은 통합 runtime
설정을 공유하며 encoder만 다르다. 하위 `config/local` 경로의 `path_base`는 `../..`이다.

자동 모델 선택기 소유권은 runtime 루프에 한정한다. GUI 상태 조회는 생성 시 확정한
가용 여부와 atomic 설정값만 읽으며 교체되는 unique_ptr를 참조하지 않는다.

## DXRT 엔진 소유권

탐지와 RepVGG는 각자의 `DxrtSession`을 소유한다. 세션은 엔진 생성·해제,
옵션 변환, 텐서 조회, SDK 제출·대기를 담당한다. 탐지 파이프라인은 입력
버퍼와 콜백 합류를 관리하며, ReID 모델은 동기 추론과 임베딩 계약을 담당한다.

`NpuCoreReservation`은 프로세스 내부에서 DXRT 코어 중복을 엔진 생성 전에
검사한다. 현재 DXRT 설정이 장치를 따로 고르지 않으므로 예약도 같은 장치
범위를 가정한다. 세션은 엔진을 먼저 파괴하고 예약을 반환한다.

런타임은 모델 요청 접수와 완료를 `ModelChangeRequests`에, 자동 관측 처리와
전환 요청을 `AutoModelCoordinator`에 위임한다. 모델 매니저는 엔진 전환과
복구를 담당한다. 녹화 조회는 `RecordingArchive`가 처리한다. `GuiSender`가
영상과 메타데이터에 대응하는 RTP 시각을 계산하고 `GuiMetadataSender`가
패킷 직렬화와 UDP 소켓을 담당한다.

## 파이프라인 성능 계측과 입력 버퍼

측정이 활성화되면 PipelinePerformance가 카메라 수신과 추적 발행을
5초 단위로 집계한다. 카메라 큐에서 교체된 프레임만 드롭으로 계수하며,
카메라에서 수신되지 않은 프레임은 이 드롭 수에 포함하지 않는다.
발행 간격과 카메라 수신부터 추적 발행까지의 지연은 별도 p95로 기록한다.

DXRT 탐지 세션은 max_inflight만큼 입력 버퍼를 준비한다. 제출 실패 시
슬롯을 반환하며, 성공한 요청은 SDK 대기와 완료 콜백 반환 뒤 슬롯을 반환한다.

## 선택형 타일 탐지와 BoT-SORT 특징 작업

`model.tile_path`를 지정한 own9e s1 프로필은 전체 및 타일 DXNN의 SHA-256을
확인한 뒤 코어 0·1에 각각 세션을 생성한다. 타일 파이프라인은 전체
640×480 원본에 model-bank 방식의 1·99백분위 명암 변환을 적용한다.
좌우 384×480 타일은 128px 겹친다. 후처리는 타일별 NMS를 생략하고
원본 좌표로 이동한 박스의 중심으로 좌우 소유 범위를 나눈 뒤 전역 NMS를
수행한다. 선택된 검출의 관절을 함께 전달한다. 한쪽 실패 시에도 양쪽
요청이 끝난 뒤 한 번만 결과를 전달한다.

BoT-SORT에서 `tracking.appearance_async: true`를 지정하면 트래커가
ReID worker를 생성한다. worker의 crop·임베딩 추출과 추적 스레드의
후보 분류가 겹치며, 추적 상태는 특징을 받은 뒤 원본 탐지 순서대로
갱신한다. worker는 트래커가 소유하고 ReID 세션보다 먼저 합류한다.

타일 실험은 `config/detection/yolo26m_pose_own9e_s1_tiles.yaml`을 탐지
카탈로그 및 초기 탐지 모델로 선택하고 `detection.inference.npu_cores: "01"`
을 지정한다. ReID를 함께 사용하면 코어 2를 지정한다. 두 모델 파일은
YAML의 경로에 설치해야 한다. DXRT 활성 빌드는 모델 해시 검사에
OpenSSL Crypto를 사용한다.

## 탐지 NPU 프레임 투입과 카메라 복구

`detection.inference.npu_feed`의 기본값은 `single`이다. 전체 프레임
모델에서 `npu_cores: "01"`과 `alternate` 또는 `auto`를 설정하면
`DetectorFactory`가 코어별 세션 둘을 생성하고 `FrameFeedPipeline`에
넘긴다. 세션마다 한 프레임 전체를 처리하고 결과 순서 및 모델 세대 검사는
기존 DetectionThread에서 수행한다. `auto`는 0번 코어로 시작해 카메라
주기와 처리 시간의 이동평균을 비교한다. 연속 초과 시 교대하고 충분히
빠른 상태가 지속되면 단독 사용으로 복귀한다. 정지나 모델 교체에서는
두 세션 모두 완료를 기다린다. 코어 1도 실행 중 전환을 위해 예약 상태다.
같은 코어를 쓰는 상주 모델 또는 타일 모드와 조합할 수 없다.

`CameraThread`는 유효 프레임이 일정 시간 돌아오지 않으면 자기 스레드에서
SDK 핸들을 닫고 다시 연다. `camera_recovery.attempts`, `stall_ms`,
`retry_ms`가 재시도를 제한하고 `identical_frames`는 원본 표본 반복
진단을 선택적으로 켠다. 복구 후 카메라 frame ID는 계속 증가한다.
TCP의 카메라 다시 열기 명령은 요청만 게시한다. SDK 함수 자체가 반환하지
않는 상황에서는 현재 프로세스 내부 복구를 수행할 수 없다.
