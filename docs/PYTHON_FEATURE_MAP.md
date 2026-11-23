# Python → Runtime → PC 기능 대응 및 구현 기준

조사 기준: ZIP `LWIR_run_code_as_is_20261111.zip`, Runtime `6238978`, PC `8e91e3e`.
Python 경로는 `live_server/tracker/` 기준이다. 표의 상태는 코드 분석 결과이며 실기 통과를 뜻하지 않는다.
Runtime 개발 브랜치는 `feat/python-runtime-integration`. PC는 Phase 3 전까지 읽기 전용이다.

2026-11-12의 최신 `model-bank/tracking_server/` 추가분과 현재 구현 상태는
[`MODEL_BANK_DELTA_20261112.md`](MODEL_BANK_DELTA_20261112.md)를 우선 확인한다.
GUI 방향도 기존 Qt 화면 재구현에서 **원본 Python 프로젝트의 HTML/CSS/JS 화면 유지와
호스트 PC 데스크톱 LAN 브리지**로 변경됐다. 이 문서의 PC 열은 과거 스냅샷이다.

## 실제 실행 분기

`run/run_real_gui.sh`는 `lwir_tracker_m1.py main()`에 w480, ByteTrack, no-vit,
class-gate, GMC(300 corners/minDistance 5), n-init 3, warm-min 45000, edge-birth .5,
RepVGG relabel, async refresh, gate JSON, judge JSON, judge-crops 2를 전달한다.
그러나 `main()`의 LIVE 분기는 `mk_tracker_()`를 사용한다. `GateTracker`는 offline
분기에만 연결되어 있어 라이브 GRU confirmation은 실제로 비활성이다. GUI도 gate_available=false다.
`last_selection.json`이 존재하면 모델/Tracker/정책 선택을 복원하므로 스크립트 인자만으로
현재 실행 상태를 단정할 수 없다. 예측 제어 lead는 재시작 시 OFF다.

## 기능 대응표

| Python 기능 / 실제 구현 | 기본·선택 상태 | 기존 Runtime 대응 | PC 대응 | 추가 작업 / 검증 |
|---|---|---|---|---|
| Detection `main.Decoder`, `native_post.Decoder`, `post.cpp decode_raw/nms_decoded` | w480 기본 | LwirPreprocessor, Yolo26Postprocessor, DxrtDetectionPipeline | VideoReceiver | percentile mapping/새 모델 계약, decode 좌표 golden test |
| 모델 변경 `model_request`, MODEL pending | GUI 선택 | ModelManagerThread 복구·generation 구현 | ControlClient requestModel, MainWindow 모델 UI | 이름·상태 동적 조회, 실패·재연결 테스트 |
| 자동 등록 `bank_probe/bank_scan/bank_refresh` | 기본 스캔 | 정적 YAML catalog | 현재 YAML 모델 label 사용 | 파일명 fallback 금지; YAML 계약 검증 후 등록, 누락·변조·부분복사 테스트 |
| tile `join_tiles`, DET rt480 | CLI 선택, live catalog 제외 | 단일 detector만 있음 | 모델 선택 | composite detector와 overlap/offset/NMS golden test 필요 |
| ByteTrack `Tracker.update` | 기본 confirm3 | ByteTrackTracker | Tracker 프로토콜만 정의 | n-init 선택, 기본 회귀 |
| BoT-SORT `BotSortTracker._dists` | GUI 선택 | BotSortTracker의 detection-index appearance 구현 | Tracker UI 미연결 | 기존 교차 테스트 재실행; GMC 연결 |
| OC-SORT `AltTracker._ocm/_take/update` | GUI 시험 옵션 | ByteTrack의 OCM/OCR/ORU 재사용, ocsort preset 추가 | 미연결 | Factory 선택 CPU 검증; 실제 Sequence 비교 미실행 |
| SORT `AltTracker(kind=sort)` | GUI 시험 옵션 | 공통 Kalman/IoU, sort preset 추가 | 미연결 | low pass 비활성/confirmation CPU 검증; 실제 Sequence 비교 미실행 |
| Buffered IoU `AltTracker(kind=biou)` | 코드 존재, live preset 제외 | buffer_scale/max_px/small_size/snap 구현 | 미연결 | 기존 기능 유지; Python의 두 단계 확대와 차이 기록 |
| Appearance `sync_embed`, `rl_embed`, `reid_catalog/reid_swap` | RepVGG 기본, model 선택 | IReidPipeline/ReidFactory/PostReidTracker | 미연결 | 모델별 계약·gallery 격리·실패 복구 |
| KeepID `relabel_online.OnlineRelabel.step` | judge 없을 때 | ReidMatcher 유사 정책 구현 | 미연결 | crop/decision 시점/threshold 차이 대조; 출력 ID diff |
| LR Judge `OnlineJudge.step`, pair_features/group_features/predict_json | --judge 기본 | 없음 | 미연결 | birth-frame K=0, 90-frame 후보, 결측값·rank·gallery parity |
| GRU32 `GateTracker._fvec/_window`, NumpyGRUGate | live 비활성, offline 선택 | 없음 | 원본 live GUI도 제외 | [11,19] features→hidden32→probability, golden vectors와 confirmation 검증 |
| GMC `GMC.apply`, GKF, gmc_warp | 기본 ON | FramePolicyTracker sparse flow 추정 및 ByteTrack/BoT warp 연결 | 미연결 | translation/reset/textureless CPU 통과; Python percentile 영상 매핑 미반영 |
| Class Gate `Tracker._cg` | 기본 ON, 사람 대 비사람 그룹 | 선택적 person-group gate 추가, 기존 기본값 유지 | 미연결 | 클래스 변경 및 최대 match threshold CPU 검증 |
| Warmth `main` raw_i_ percentile 분기 | live 45000 기본 | FramePolicyTracker raw16 stride2 p95 min/max | 미연결 | percentile/타 class/중복 detection source index CPU 검증 |
| Edge Birth `edge_ok` | .5 기본 | ByteTrack/BoT-SORT의 신규 birth만 차단 | 미연결 | 기존 low-score match 유지 CPU 검증; 장치 재검증 필요 |
| Lost/Kalman `Tracker.update`, KF | 기본 | KalmanFilter, track_buffer, lost_output_frames | predicted flag 표시 | gap/reset 상태 검증, 기존 동작 유지 |
| Motion GRU `PosGRU.step`, pos_predict | 선택, 기본 Kalman | 없음 | 미연결 | 16 features→hidden40→4 residual; NPZ 변환/동등성/상태 reset |
| Motion Mamba `PosMamba.step`, pos_predict | 선택, 기본 Kalman | 없음 | 미연결 | recurrent conv/SSM→4 residual; NPZ 변환/동등성 |
| 영상 `LiveServer`, GimbalBridge GUI link | 웹+기존 LAN 동시 | GuiSender H264/RTP/UDP, metadata | VideoReceiver/MetadataReceiver/FrameSynchronizer | 기존 LAN만 확장; timestamp 일치/late packet 검사 |
| ID 클릭 / AUTO / MANUAL / STOP / HOME `GimbalBridge`, live_page post/release | 기본 GUI | TargetSelector, GimbalCommandExecutor, AutoTrackingController 구현 | VideoWidget/ManualInputHandler/ControlClient 구현 | HOME/STOP 접근 UI와 상태·timeout·disconnect 검증 |
| 예측 제어 `lead_shift` | 기본 OFF | 없음 | 미연결 | GMC 보정 velocity, smoothing .7/.3, bounded lead; timeout/세대 reset |
| 제어 설정 `gimbal_bridge_v25` CLI | 시작 인자 | typed ControlConfig/PID 구현 | runtime 변경 UI 없음 | ControlThread 단일 명령 경로의 설정 적용/실패 결과 |
| FPS/latency `/perf`, perf history | 기본 수집 | StageMetric/DetectionTiming/MetricsWriter | PerformanceRecorder/RuntimeTelemetryModel | 현재 Runtime branch와 PC telemetry wire 지원 비교, 실제 FPS 측정 |
| 측정 `measure_*`, live_page mpost/history/render | GUI 시작/중지·이름·메모 | 시작 시 MetricsWriter만 있음 | PC 자체 recording 있음 | Runtime 세션 API/통계/CSV/JSON/이력 |
| raw 녹화 `LiveServer` zlib measurement writer | 측정 옵션 | FrameContext 원본 있음 | 제어 UI 없음 | bounded writer queue, 16-bit round trip/drop/error/flush 검증 |
| 카메라 `cam_pipe.cpp`, LiveServer reset | 오류 reopen 및 GUI | CameraThread open/capture/close | 상태 UI 없음 | 같은 owner thread에서 reset/recovery, cancellation 테스트 |
| 시스템 상태·재부팅 `live_page syspost` | 명시 GUI 동작 | RuntimeControl/오류 일부 | 연결 오류 표시 있음 | 상태 조회, 권한·확인 있는 reboot; 자동 실기 재부팅 금지 |
| 설정 저장 `sel_save`, SEL_FILE | live 재시작 복원 | YAML 초기 설정 | PC config loader | 허용된 값 atomic 저장·검증; 안전상 AUTO/lead 재활성 금지 |
| 영상 안정화 `Stabilizer`, stab_live/warp/rows_real` | 선택 OFF | 없음 | 없음 | 원본 좌표 계약을 유지하는 별도 검토 필요 |
| zoom/late ViT/lost-first/Mamba relabel | CLI 연구 옵션 | 없음 | live GUI 노출 아님 | 기본 기능과 분리; 필요한 추가 가중치/코드 미포함 여부 확인 |

## 가중치와 알고리즘 제한

`motion/pos_gru.json`, `pos_mamba.json`은 모두 **시험용 1 epoch, 미검증**으로
명시되어 있다. 추론 수치 동등성은 실사용 품질 검증이 아니다. GRU32 confirmation,
LR relink, GRU/Mamba motion, 별도 `--mamba` relabeler는 서로 다른 모델이다.
ZIP의 setup.sh는 `../mamba/SHA256SUMS`를 요구하지만 해당 폴더는 ZIP에 없다.
`setup.sh`가 빌드하는 gimbal_bridge와 run_real_gui.sh의 v25 바이너리 이름도 다르다.

## 구현 배치와 단계 검증

Tracker 정책·GMC 추정은 Factory가 구성하는 frame-aware Tracker 객체가 소유한다.
Thread에는 알고리즘 종류별 분기를 추가하지 않는다. Raw 영상은 shared FrameContext를
bounded writer queue에 전달한다. 모델/정책 변경은 Manager 후보 생성과 epoch 교체를
재사용한다. TCP에는 catalog/query/change/result만 추가하며 accepted/completed를 구분한다.

단계별 상태와 실행 증거는 PROJECT_PROGRESS/TEST_RESULTS에 갱신한다. 표의 추가 작업은
구현 완료 표시가 아니며, 기존 구현도 이번 작업에서 테스트하기 전에는 새 통과로 보고하지 않는다.

## 2026-11-23 model-bank 웹 UI 이식 준비

조사 기준은 model-bank `0e3dd54`의 `tracking_server/`와 Runtime
`refactor/pipeline`의 `fc5e6eb`이다. 인접 워크스페이스에서
`pc-application` 또는 `desktop_gui.py`를 찾지 못했다. 과거 이식 기록의
`lan_protocol.py`, `desktop_routes.py`, `desktop_gui.py`는 PC 저장소를
확보하면 확인할 기존 진입점이다.

### 원본 구성과 이식 위치

- `page/parts.json`에 나열된 HTML/CSS/JS 34개 조각을 `build_page.py`가
  `live_page.html`로 합친다. 화면 작업은 조각에서 하고 `--check`로
  생성본 일치를 확인한다. 특히 `picture_stream.js`, `picture_first.js`와
  각 패널의 fetch 호출이 전송 방식에 결합된 경계다.
- `src/web/page_handler.py`가 페이지·설정·제어·측정·재생 요청을 분기한다.
  `preview_worker.py`와 `stream_sender.py`는 서버 영상 미리보기·전송을,
  `direct_listeners.py`는 추가 접속 주소와 PC 다운로드 경로를 맡는다.
- PC의 WebKit 화면과 LAN 브리지 사이에서 페이지 데이터와 조작을 연결한다.
  PC 영상 디코더는 UDP 5000 H.264/RTP, 추적 표시는 UDP 5001, 장비 명령은
  TCP 5002를 사용한다. 패킷 계약은 [PC 통신 명세](pc_communication.md)에 있다.

| 원본 화면/API | 이식 시 사용할 계약과 남은 기능 |
|---|---|
| `/stream`, `/overlay`, `/snap.jpg`, `/time` | PC에서 디코딩한 영상과 해당 RTP timestamp의 메타데이터를 결합한다. 스냅샷은 PC의 표시 프레임으로 만들 수 있다. 카메라부터 화면까지의 지연은 시계 보정 및 카메라 기준 시각이 필요하다. 현재 UDP의 `gui_started_us`는 GUI 송신 경로 진입 시각이다. |
| `GET /model`, `POST /model/set`, `/rules`, `/rules/set` | Pipeline 1–13으로 카탈로그, ID 전환, 활성 Tracker 정책·일부 기능과 자동 탐지 설정을 연결한다. 모델·Tracker 전환은 Completed 후 활성 ID를 다시 조회한다. 이름은 표시용으로 쓰며, 카탈로그 열거는 등록 가능한 ID 범위를 확인한다. Python의 모든 옵션과 실시간 NPU feed 설정은 연결 범위 밖이다. |
| `/gimbal/people`, `/gimbal/ping`, `/gimbal/click-frame`, `/gimbal/move`, `/gimbal/stop`, `/gimbal/home` | 화면에 표시한 프레임에서 Track ID를 고르고 기존 AUTO로 전달한다. 제어 가능 여부와 대상 유효성은 tracking handler가 판단한다. 클릭 프레임의 키는 TCP AUTO에 없으므로 원본과 동일한 서버측 클릭 프레임 검증에는 별도 계약이 필요하다. 수동 입력을 놓으면 MOVE(0,0), 자동 추종 해제에는 MANUAL을 사용한다. HOME은 handler 응답에 따르며 POSITION의 추정 백분율은 실제 각도 눈금에 사용하지 않는다. |
| `/perf`, `/perf/deadlines`, `/follows` | PC에서 선택적 UDP telemetry로 수신한 지표와 수신 FPS를 그래프로 만들 수 있다. 데드라인 이력, 추종 실패 원인 이력, 원본의 서버 측 집계 지표는 별도 데이터가 필요하다. |
| `/measure/*`, `/replay/*` | Recording 1–6으로 녹화 시작·정지·상태·완료 목록·내보내기를 연결한다. 원본의 메모 편집, 영상 없는 측정, GT 평가 및 가상 추종은 별도 구현 대상이다. PC 재생은 내보낸 프레임과 `tracks.csv`를 frame ID로 결합하는 것부터 시작한다. |
| `/network`, `/netinfo`, `/pc/*`, `/healthz`, `/diagnostics/identity`, `/tracker/camera`, `/system/*`, `/camera/reset` | 연결 상태는 PC의 소켓과 실제 수신 상태로 표시한다. PC 다운로드, 보드 관리, 카메라 리셋, 실시간 NPU 투입 변경은 별도 기능이다. 대응 명령이 없는 버튼에는 명시적 미지원 상태를 표시한다. |

### 브리지 구현과 인수 기준

1. PC 저장소를 확보해 기존 세 모듈과 화면 버전을 확인한다. 원본 페이지 조각에서
   DOM·표시 로직을 재사용하고, HTTP 호출에 대응하던 부분을 PC 브리지 호출로 연결한다.
2. 첫 화면은 연결 상태, 영상, 동일 RTP timestamp의 박스, 모델·Tracker 목록과
   전환 결과를 포함한다. RTP 타임스탬프는 32비트이므로 연결 세션마다 짧게
   보관하고 재접속 시 비운다. 박스 매칭에 실패하면 영상만 표시한다. 화면 좌표를
   640×480 원본 픽셀로 변환할 때 레터박스 여백을 제외한다.
3. 수신 PC에서 TCP 부분 수신·묶음 수신, Accepted 이후 Completed 실패,
   전환 중 단절 및 재접속, UDP 순서 뒤바뀜·누락, 클릭 대상 유효 기간을 검증한다.
   TCP에 프로토콜 버전 필드가 없고 모르는 명령은 연결을 닫을 수 있으므로
   배포 대상 Runtime과 명령 세트를 함께 확인한다.
4. 이후 정책 설정과 telemetry, 녹화 전송을 연결한다. 녹화 재생은 PC에서
   먼저 검증하며 재생 화면의 입력은 실시간 제어로 보내지 않는다.
