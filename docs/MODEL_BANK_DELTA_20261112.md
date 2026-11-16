# model-bank 최신 기능 이식 대장

## 2026-11-16 원격 main 갱신 대조

이전 기준 `3897aaf`에서 원격 `main` `5ea5b64`까지 244개 커밋이 추가됐다.
현재 `feat/python-runtime-integration` 및 PC GUI의 실제 구현과 비교한 상태다.
`model-bank`의 Python 서버·HTTP 경로는 이식 대상이 아니며, 장치/GUI 기능을
기존 C++ Runtime의 스레드·Queue 및 단일 PC LAN 경계에 연결한다.

| 기능 | 현재 Runtime/PC 상태 | 남은 작업 |
| --- | --- | --- |
| 16비트 원본 녹화 | 호스트 구현: gzip 원본, 프레임 시각·Track CSV, LAN 시작/종료/상태, PC 버튼 | 장치 실카메라·장시간 디스크·PC LAN 검증, 목록/재생 |
| C++ SORT/OC-SORT/BoT-SORT | Runtime에는 자체 C++ Tracker가 이미 있음. Python 서버의 확장 모듈을 그대로 복사할 필요 없음 | 동일 검출 입력/설정에서 결과·ID/지연 비교, 신규 정책 차이 선별 |
| 새 근거리/범용 자세 검출과 관절 | 기존 Runtime 카탈로그와 후처리는 여섯 split head 모델까지 | 모델 파일 SHA/Tensor 확인, 9 출력 관절 풀이, Runtime 모델 카탈로그·보드 추론 |
| 자동 검출 모델 선택·두 모델 상주 | 미구현 | 기존 ModelManager/세대 교체 경로 확장, NPU 자원·전환·복구 검증 |
| 관절 기반 위치 기억 GRU/Mamba | 미구현 | 모델/입력 계약·실제 데이터 평가, Tracker 내부 정책 연결 |
| 상자 다듬기 size/body·lost_same | 미구현 | 표시/짐벌 동일 박스 계약과 예측 표기, 기존 제어 경로 유지 |
| NPU 교대·자동 투입·주기 감시 | Runtime은 max_inflight와 정해진 core binding만 지원 | DXRT 자원·순서·드롭 정책 실기 비교 뒤 선택 기능 추가 |
| 재생·재생 중 채점 | 미구현 | Camera 입력 선택, 안전 STOP, 원본 프레임 시간표, 선택 시퀀스 평가 |
| 화면 지연·영상/메타데이터 동기화 | PC에서 수신율만 표시하고 최신 metadata를 영상에 그림 | RTP timestamp 결합 및 실제 LAN 지연 측정 |
| 짐벌 소실·탐색 후보 | 기존 Runtime의 단일 GimbalCommandExecutor 제어 사용. 신규 후보 미이식 | 모델뱅크에서 운영 채택된 조건만 분리해 실기 안전 검증 |
| 시스템·카메라 관리 | PC 원본 화면의 미지원 버튼은 실패 응답 | Camera 소유권·서비스 상태 계약 뒤 LAN 명령 추가 |

이 표는 이식 완료 선언이 아니다. 특히 모델뱅크의 장치 적용·PC 모의 시험은
현재 Runtime/GUI가 통과한 증거가 아니다. 아래 11월 12일 표는 당시 기준의
기능 배치 결정으로 남긴다.

확인 기준: 독립 저장소 `model-bank`의 `main` / `3897aaf572fae1d87f9c8cc3d7db85028cc79501`
(2026-11-12), `tracking_server/` 및 `label_server/` 소스와 `HANDOFF.md`.
이 파일의 상태는 현재 호스트 코드 기준이다. Python 서버의 동작이나 PC 모의 시험을
새 C++ Runtime의 실기 통과로 간주하지 않는다. 기존 ZIP 전체 대응표는
[`PYTHON_FEATURE_MAP.md`](PYTHON_FEATURE_MAP.md)에 있다.

## 배치 원칙

- Orange Pi는 기존 C++ Runtime/Benchmark의 Camera → Detection → Tracking →
  GUI/Control과 YAML/Factory/Manager/Queue를 유지한다.
- PC 화면의 원본 `tracking_server/page/html`, `css`, `js`와 빌드된
  `live_page.html`을 디자인·상호작용의 기준으로 사용한다. 해당 JavaScript는
  `/rules`, `/model`, `/measure`, `/perf`, `/replay`, `/gimbal`, `/stream`을
  호출하므로 **서버 없이 파일 그대로 실행할 수 없다**. 화면 코드는 유지하고
  HTTP 호출과 영상 입력 경계만 호스트 데스크톱 브리지로 바꾼다.
- Runtime은 기존 UDP 영상/메타데이터와 TCP 명령 경로를 확장한다. Python
  HTTP/MJPEG 서버, 별도 WebSocket 서버, 두 번째 짐벌 제어기는 사용하지 않는다.
- `label_server`는 라벨 제작 도구로서 Runtime 추론 경로와 분리한다.
- `pc-application/python_gui/desktop_gui.py`는 이 화면을 Gtk/WebKit2에
  로드하고 기존 UDP/TCP만 사용한다. 현재 모델·Tracker 변경, 정책 숫자 필터,
  상태 조회와 대상 클릭/수동 제어를 연결했다. 나머지 호출은 명시적 실패를
  반환한다. 개발 중 화면은 원본 model-bank 페이지를 별도로 표시한다.

## 최신 model-bank 기능

| model-bank 근거 | 배치 | 현재 상태 | 남은 검증·구현 |
| --- | --- | --- | --- |
| `src/track/confirm_time.py`, `3c2d819`, `033eef6`: 130 ms 확인, 50개 간격 중앙값, 0.15장 hysteresis | 공통 `FramePolicyTracker` → ByteTrack/BoT-SORT | `confirmation_ms` 선택형 구현, Python 참고 프리셋만 130 ms, Debug CPU 테스트 통과, TCP에서 현재 계산 장수 조회/변경 가능 | 실제 카메라 FPS와 DXRT 교체 검증 |
| `src/track/warm_p95.py`, `515c7b5`: partition 기반 95백분위 | 기존 `FramePolicyTracker::warmth_percentile` | C++ `nth_element`로 동일 순위 선택을 이미 수행; 중복 구현 없음 | Python과 원시 영상 golden 비교, 장치 지연 측정 |
| `src/app.py`, `d336c36`: 다음 프레임 전 결과 발행 | `DetectionThread` callback/Queue | 비동기 완료 시 즉시 게시하는 기존 경로 확인; 중복 구현 없음 | 실제 DXRT 지연 비교 |
| `src/common/stage_timing.py`, `LATENCY.md`: 단계별 지연 | `StageMetric`/`DetectionTiming`/Benchmark | 기존 단계 기록 존재 | Python 지표 정의 대조, PC 그래프와 세션 API 연결 |
| `src/camera/camera_extra.py`, `replay_cam.py`, `replay_link.py`, `cca77e3`: 녹화 재생, seek/pause/speed/loop | Camera 수명주기의 선택형 입력 소스 + PC 플레이어 | 미구현 | 카메라와 상호 배타, 재생 중 짐벌 STOP, 원본 16비트 계약, 장시간 메모리 경계 |
| `src/track/gt_score.py`, `src/camera/replay_eval.py`, `72ed2c6`: 재생 중 GT 점수·무시 구역·결과 JSON | Benchmark 평가 재사용 + PC 표시 | 미구현 | 라벨 매칭, IDSW/IDF1/MOTA, 실시간 결과와 최종값 일치 |
| `src/web/measure.py`, `9744ae4`: 측정 세션 및 16비트 raw.gz 재생 | Runtime Measurement/원본 Writer + PC 측정 화면 | 시작 시 MetricsWriter만 있음 | bounded writer, 시작/중지/이력/CSV/JSON, raw round trip |
| `src/gimbal/v27/`, `gimbal_pose.js`, `bd04926`: 명령 기반 짐벌 위치 추정과 눈금 | 단일 `GimbalCommandExecutor` + 기존 TCP + 원본 PC 화면 | 전송 성공한 명령으로 추정값을 계산하고 TCP 조회·PC 화면에 연결. 호스트 CPU 테스트 통과 | 추정값은 실제 각도가 아님. 장치 표시·방향 및 안전 검증 미실시 |
| `src/gimbal/v27/`, `HANDOFF.md`: v27 제어 개선/HIL | 기존 ControlThread/AutoTrackingController | 별도 Python bridge 사용 안 함, v27 기능 미이식 | 한 축 속도 결합 문제, 단계별 장치 시험은 별도 안전 조건 필요 |
| `src/camera/camera_extra.py`, `src/detect/frame_feed.py`: 카메라 stamp/복구와 코어 번갈아 넣기 | CameraThread와 Detection 설정 | 전자는 부분 구현, 후자는 미구현·원본에서도 기본 OFF | 기존 async/generation 보존, 원본 기준 미달 코어 교대는 성능 검증 전 기본 OFF |
| `label_server/scripts/pick_frames.py`, `78cbf53`: 라벨 우선 프레임 추출 | PC 호스트의 오프라인 라벨 도구 | 미이식 | Runtime/추론에 섞지 않고 입력·출력 경로와 선택 규칙 검증 |

`model-bank`의 페이지에는 카메라 초기화와 재부팅 버튼도 있다. 기존 Runtime
Camera 소유권과 시스템 권한을 확인하기 전에는 버튼을 동작하는 것처럼 연결하지 않는다.
모델 파일·SHA·Tensor 계약은 별도 [`MODEL_INVENTORY.md`](MODEL_INVENTORY.md)에
기록하며, 새 파일 이름만으로 자동 실행 가능 상태로 올리지 않는다.
