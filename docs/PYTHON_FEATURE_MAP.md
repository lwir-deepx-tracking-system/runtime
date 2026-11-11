# Python → Runtime → PC 기능 대응 및 구현 기준

조사 기준: ZIP `LWIR_run_code_as_is_20261111.zip`, Runtime `6238978`, PC `8e91e3e`.
Python 경로는 `live_server/tracker/` 기준이다. 표의 상태는 코드 분석 결과이며 실기 통과를 뜻하지 않는다.
Runtime 개발 브랜치는 `feat/python-runtime-integration`. PC는 Phase 3 전까지 읽기 전용이다.

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
| Edge Birth `edge_ok` | .5 기본 | 없음 | 미연결 | 신규 birth만 차단, 기존 low-score match 유지 |
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
