# 프로젝트 진행 기록

이 문서는 **확인한 코드와 실행한 검증**을 기록한다. 다음 세션은
[`AGENTS.md`](../AGENTS.md)와 [`ARCHITECTURE.md`](ARCHITECTURE.md)를 함께 읽고
Git 상태와 대조한다. 과거 스냅샷의 경로·테스트 결과를 현재 상태로 가정하지 않는다.

## 2026-11-11 확인한 체크아웃

| 항목 | 확인 결과 |
| --- | --- |
| 저장소 | `/workspace/lwir-deepx-tracking-system/runtime` |
| 브랜치 | `porting` |
| HEAD | `62389783850f3f62244fcec4780d89cfd156a2f8` (`feat: reid 구조 변경`) |
| `feature/reid` | 확인 시점에 HEAD와 같은 커밋을 가리킴 |
| 기록 작업 시작 전 Git 상태 | 깨끗함 |
| PC GUI 저장소 | 이 워크스페이스에서 `pc-application` 디렉터리를 찾지 못함 |
| Python 참고 ZIP | `/home/johnny/Desktop/LWIR_run_code_as_is_20261111.zip` 존재; ZIP 목록만 확인, 기능 분석은 미실시 |

## 2026-11-11 기록 정리 단계

`AGENTS.md`에 유지할 구조와 새 세션 시작·단계별 기록 규칙을 추가했다.
`docs/PROJECT_PROGRESS.md`와 `docs/ARCHITECTURE.md`를 작성했다.
이 단계에서는 Runtime 소스·설정·테스트 코드를 변경하지 않았다.
현재 체크아웃에서 위 10개 관련 CPU 테스트를 재실행해 모두 통과했다.
전체 37개 테스트는 이번 기록 단계에서 재실행하지 않았다.

## 완료한 작업: Multi-Tracker (`6238978`)

기존 Runtime 스레드·Queue 구조를 유지하면서 공통 `Tracker::process(DetectionResult)`
호출을 추가했다. 기존 `track(vector<Detection>)` 호출은 유지된다.

- ByteTrack 단독은 기존 알고리즘을 사용하며 RepVGG 객체를 생성하지 않는다.
- ByteTrack + Post-ReID는 기존 `ReidMatcher`를 Tracker 세대 내부에서 실행한다.
- BoT-SORT는 Detection 인덱스에 연결된 RepVGG 특징을 Association 전에 생성한다.
  XYWH Kalman 예측, IoU/Appearance 결합, 고·저점수 매칭, gating, Lost/Removed,
  Track별 특징 평활 갱신을 구현했다. CMC 보정값 공급 경로는 구현하지 않았다.
- Factory가 Tracker와 필요한 ReID 객체를 조합한다. Benchmark와 Runtime이
  같은 구현을 사용한다. `tracking.id` 카탈로그는 1=ByteTrack,
  2=BoT-SORT, 3=ByteTrack+Post-ReID다.
- Benchmark는 MOTChallenge 형식의 `tracks/`와 `tracks_reid/`, 임베딩 CSV,
  Association/진단 이벤트, 단계별 지연 기록을 유지·확장했다.
- Runtime은 `TrackerManager`의 교체·epoch와 Detection 모델 세대 reset을
  유지한 채 `TrackingThread`에서 공통 호출을 사용한다.

### 해당 커밋의 수정 파일

| 파일 | 기록된 변경 |
| --- | --- |
| `include/tracking/tracker.hpp` | 프레임 입력과 `TrackingStep` 계약 |
| `include/tracking/botsort_tracker.hpp`, `src/tracking/botsort_tracker.cpp` | BoT-SORT 구현 |
| `include/tracking/post_reid_tracker.hpp`, `src/tracking/post_reid_tracker.cpp` | ByteTrack 사후 ReID 조합 |
| `include/tracking/tracker_factory.hpp`, `src/tracking/tracker_factory.cpp` | 종류 선택 및 ReID 조합 생성 |
| `include/config/app_config.hpp`, `src/config/app_config.cpp` | Tracker 종류·옵션 파싱과 검증 |
| `include/reid/reid_matcher.hpp`, `src/reid/reid_matcher.cpp` | 기존 매칭 정책의 후보 탈락 진단 |
| `apps/benchmark/benchmark.cpp` | 공통 호출, 결과 및 시간 기록 |
| `apps/runtime/runtime.cpp`, `src/pipeline/tracking_thread.cpp` | Runtime 연결 |
| `config/benchmark.yaml`, `config/runtime.yaml` | Tracker 카탈로그 및 ReID 설정 |
| `config/tracking/botsort.yaml`, `config/tracking/bytetrack_post_reid.yaml` | 새 Tracker 설정 |
| `include/detection/detector_factory.hpp`, `CMakeLists.txt` | DXRT 비활성 빌드 및 새 소스·테스트 등록 |
| `tests/tracking/test_botsort.cpp`, `tests/tracking/test_post_reid_tracker.cpp`, `tests/tracking/test_multitracker_config.cpp`, `tests/tracking/test_tracker_factory.cpp` | 알고리즘·ReID·설정·Factory 검증 |
| `tests/pipeline/test_multitracker_runtime.cpp`, `tests/app/test_app_config.cpp` | 교체·모델 세대·카탈로그 검증 |
| `docs/multitracker.md` | 모드 선택과 결과 파일 의미 |

### 검증 기록

| 검증 | 결과와 범위 |
| --- | --- |
| 앞선 Multi-Tracker 작업의 DXRT 비활성 CMake 전체 빌드 | 성공. `LWIR_ENABLE_DX_APP=OFF`; NPU 빌드 증거는 아님 |
| 앞선 작업의 CTest 전체 실행 | 37/37 통과. GUI/Control 포트 테스트는 샌드박스 밖에서 실행 |
| 이 기록 작업에서 현재 체크아웃 대상으로 재실행한 10개 테스트 | 10/10 통과: `botsort`, `post_reid_tracker`, `multitracker_config`, `multitracker_runtime`, `bytetrack`, `tracker_factory`, `tracker_hotswap`, `tracker_change_service`, `benchmark_config`, `app_config` |
| 실제 RepVGG DXNN·Detection 모델 추론 및 성능 | 이 체크아웃에서 미검증 |
| 실제 데이터셋 IDSW/IDF1/HOTA | 측정 결과 없음. 개선으로 보고하지 않음 |

테스트의 Fake ReID 임베딩은 알고리즘·배선 검증용이며 DXRT 출력 검증이 아니다.
`docs/multitracker.md`에 세 모드의 파일 의미와 Crop 정책을 기록했다.

## 착수하지 않은 작업: Python 기능의 C++ Runtime + PC Qt GUI 이식

다음 사용자 요청은 Python ZIP의 실제 실행 경로와 기존 Runtime·PC GUI를
분석한 뒤 기능 대응표를 만들고 필요한 기능을 이식하는 것이다. 해당 요청은
분석 시작 안내 후 중단되었으며, **이 요청을 위한 코드 변경·기능 대응표·빌드·테스트는 없다.**
ZIP 내부 목록에는 `live_server/gimbal/`과 여러 버전의 파일이 있지만,
어느 파일이 실제 실행 코드인지는 아직 판정하지 않았다. PC GUI 저장소가
현재 워크스페이스에 없어 PC 구현·프로토콜 호환성도 확인하지 못했다.

## 남은 문제와 다음 구현 순서

1. PC GUI 저장소의 실제 위치와 브랜치·작업 변경을 확인한다. Python ZIP의
   실제 진입점, 실행 중 기능, 선택 기능, 과거/시험 코드를 분류한다.
2. Runtime과 PC GUI의 현재 화면·명령·텔레메트리 기능을 직접 조사하고,
   Python 기능별 대응표를 코드 위치와 근거를 붙여 작성한다.
3. 이미 구현된 기능을 재사용하며 빠진 프로토콜/UI 연결부터 단계별로 구현한다.
   모델·Tracker 목록, ReID/필터, 측정·녹화, 시스템 관리 등의 범위는
   대응표와 실제 계약 검증 후 확정한다.
4. 각 단계마다 CPU 빌드·관련 회귀 테스트를 실행하고 이 문서를 갱신한다.
   Orange Pi에서 DXRT 모델 로딩·추론, 코어 경합, 짐벌 안전 동작을 별도 검증한다.
5. 동일 Detection 입력과 실제 데이터셋이 준비되면 세 Tracker의
   IDSW/IDF1/HOTA와 지연을 측정한다. 현 Runtime 설정은 Detection과 ReID가
   모두 `all` NPU 코어를 선택하므로 실제 장치에서 자원 경합도 확인한다.

## 기록 갱신 규칙

단계 완료 시 날짜·브랜치·HEAD·작업 전후 Git 상태, 완료 내용, 파일 목록,
실행한 명령과 결과, 실행하지 못한 검증, 다음 순서를 추가한다.
실행하지 않은 테스트나 하드웨어 작업을 성공으로 기록하지 않는다.

## 2026-11-11 통합 준비 및 SSH 인증 설정

- 호스트 Runtime: `porting`, HEAD `62389783850f3f62244fcec4780d89cfd156a2f8`.
  `feature/reid`도 같은 SHA다. 시작 전 변경은 `AGENTS.md`와 미추적 문서 두 개였으며 보존했다.
- PC 저장소를 `/workspace/lwir-deepx-tracking-system/pc-application`에 복제했다.
  `main`, HEAD `8e91e3eb562a56f9fad159fdf69aec6603d2beb0`, 작업 트리 깨끗함을 확인했다.
- Python ZIP을 `/tmp/lwir-python-reference-20261111`에 풀고 실행 스크립트,
  모델 Manifest, 주요 Tracker/GUI 코드 분석을 시작했다. 전체 대응표는 아직 미완료다.
- 사용자 최신 지시에 따라 로컬 인증 파일 `.local/orangepi-ssh.json`을 만들었다.
  권한은 `600`, 상위 디렉터리는 `700`이다. `.gitignore`에서 `.local/` 전체를 제외하며
  `git check-ignore -v`로 확인했다. 인증 값은 문서나 Git 파일에 기록하지 않는다.
  로컬 SSH helper는 비밀번호를 argv/환경 변수에 넣지 않고 echo가 꺼진 SSH 프롬프트에 전달한다.
- 수정 파일: `.gitignore`, `AGENTS.md`, 이 문서, `docs/TEST_RESULTS.md`.
  로컬 인증 파일과 helper는 미추적·제외 대상이며 배포 산출물에 포함하지 않는다.
- 키 인증은 거부되었으나 로컬 인증 파일을 사용한 SSH 접속은 성공했다.
  원격 저장소 `/home/orangepi/workspace/runtime`, 브랜치 `feat/runtime-telemetry`,
  SHA `71eb9c84922723176ca8312a5b87bddff82d2387`.
  기존 미추적 파일은 `tests/tracking/lwir_to_mot.py`다.
- 원격 조사는 읽기 전용이었다. 브랜치 전환, 소스 수정, 서비스 중지, NPU 추론은 하지 않았다.
  `dxrt.service` active, `/dev/dxrt0` 존재, 짐벌 브리지 실행을 확인했다.
  `cam_pipe`는 최초 조사에서 실행 중이었고 다음 조회에서는 보이지 않았다. 이 작업에서 중지하지 않았다.
- 모델 파일 SHA를 호스트 Manifest와 비교했다. NEWMRW480, NEWMRT480A/B,
  RVGGA0_REID2C256, S480, S6, M640의 일치 파일을 찾았다.
  기존 NEWMRT480A/B 심볼릭 링크의 대상 이름은 실제 `_nms_480x384.dxnn` 파일 이름과 달랐다.
  경로를 수정하거나 모델을 실행 가능 상태로 등록하지 않았다.
- 원격 `parse_model -v`로 NEWMRW480의 입력 `[1,480,640,3] uint8`과
  `box_s8/16/32`, `cls_s8/16/32` float32 출력 계약을 읽었다.
  이것은 메타데이터 확인이며 DXRT 로딩·추론 통과 증거가 아니다.
- 다음 순서: 전체 기능/모델 대응표 완성 → 호스트 개발 브랜치에서 Benchmark 구현·검증
  → Runtime 구현·검증 → Commit/Push → 보호된 원격 테스트·복원 → PC 기능 확장.
  전체 통합과 실기 검증은 아직 완료되지 않았다.

## 2026-11-11 Phase 0 조사 및 Phase 1 부분 구현 (최신)

이 절이 위 과거 스냅샷보다 우선한다. 전체 이식은 아직 미완료다.

- 호스트 작업 브랜치: `feat/python-runtime-integration`, 기준 HEAD
  `62389783850f3f62244fcec4780d89cfd156a2f8`. 이번 변경은 아직 미커밋이다.
- 세 소스 모두 접근 확인. PC는 `main`/`8e91e3eb562a56f9fad159fdf69aec6603d2beb0`로
  분석만 수행했으며 소스 수정은 하지 않았다.
- `PYTHON_FEATURE_MAP.md`에 실행 함수, 기본/선택/시험 기능, Runtime 및 PC 대응과 검증을 기록했다.
  실행 스크립트의 GRU32 gate 옵션과 달리 live 경로는 해당 gate를 사용하지 않는다.
  Motion GRU/Mamba 가중치는 원본 메타데이터에서 시험용·미검증으로 표시된다.
- `MODEL_INVENTORY.md` 및 `model_evidence/remote_metadata_20261111.json`에
  11개 모델의 Manifest SHA 일치와 실제 DXNN 메타데이터를 기록했다.
  S640/FT1/FT9는 조사 경로에서 일치 파일 미확인. 모델 로딩·추론은 미실행이다.
- Factory에서 선택적 `FramePolicyTracker`를 조합한다. Warmth percentile 필터,
  sparse optical flow GMC, 이미지 크기 공급을 맡으며 원본 FrameContext를 공유한다.
  Class Gate, Edge Birth, confirmation은 Association/Track 생명주기에 적용한다.
- 기존 OCM/OCR/ORU를 사용하는 Python 방식 XYWH OC-SORT 선택 및 low-score pass를
  사용하지 않는 SORT 선택을 추가했다. 원 논문 7-state 구현과 동등하다고 주장하지 않는다.
- 설정 파일 `ocsort.yaml`, `sort.yaml`, `bytetrack_filtered.yaml`은 선택용이며
  기존 Runtime 기본값/카탈로그를 변경하지 않았다. Python 전체 Preset 완성은 아니다.
- 변경 파일: config 구조·파서, Tracker 공통 hook, ByteTrack/BoT-SORT/PostReID,
  TrackerFactory, FramePolicyTracker/tracking_policy, CMake, `test_frame_policy.cpp`,
  위 세 YAML과 조사 문서. 스레드·Queue·Manager 코드는 변경하지 않았다.
- CPU baseline 37/37, 정책 추가 후 38/38 CTest 통과. 동일 Detection 값이 중복될 때
  필터 후 Embedding source index가 혼동되지 않도록 명시적 인덱스 매핑을 추가했다.
- 한계: GMC의 현재 16→8bit 변환은 고정 범위다. Python live percentile/EMA와의
  동일성, 모델별 전처리, LR Relink Judge/GRU Gate/Motion, 동적 설정·모델 목록,
  측정 세션·원본 녹화·카메라 초기화·PC 연결은 남은 작업이다.
- 원격은 읽기 전용 조사만 했다. 테스트 브랜치 전환·서비스 변경·실기 추론 없음.
  따라서 테스트 후 복원 완료로 보고하지 않는다. 배포 시 사전 상태를 다시 기록한다.
- 다음 순서: Phase 1 남은 알고리즘 및 모델 전처리 계약 구현·검증 → Runtime 통합
  → 호스트 Commit/Push → 원격 격리 테스트와 상태 복원 → PC 확장 → LAN 검증.

### 같은 단계 후속 검증

- 모델·참조 설정 경로에 명시적 YAML `path_base` 지원을 추가했다.
  기본 Runtime/Benchmark 및 기존 Detection/ReID YAML에 base를 선언했다.
  모델 선택과 파일 대상은 동일하다. 외부 YAML의 기존 CWD 의미는 base 생략 시 유지된다.
- `tests/app/test_app_config.cpp`, `test_benchmark_config.cpp`에서 `/tmp`로
  CWD를 바꿔도 동일 모델/Tracker 경로로 해석됨을 확인했다.
- 최신 전체 CPU 빌드 성공, CTest **38/38 통과 (3.27초)**, `git diff --check` 통과.
  중간 실패와 수정 내용은 `TEST_RESULTS.md`에 기록했다.

### 원격 상태 재확인

읽기 전용 SSH 재조회 결과: `feat/runtime-telemetry`,
`71eb9c84922723176ca8312a5b87bddff82d2387`,
`?? tests/tracking/lwir_to_mot.py`가 사전 기록과 동일하다.
`dxrt.service` active, 기존 `gimbal_bridge_v` PID 30416 유지.
이번 작업에서는 원격 테스트 브랜치 전환·서비스 변경을 하지 않았다.
이 확인은 실기 테스트 후 복원 검증을 대신하지 않는다.

## 2026-11-11 Orange Pi 기존 Benchmark 실행

사용자 요청에 따라 기존 Python 서버를 정지하지 않고 Orange Pi의 현재 체크아웃
`feat/runtime-telemetry` / `71eb9c84922723176ca8312a5b87bddff82d2387`에서
`seq_004` 281프레임 Benchmark를 실행했다. Exit 0, Detection/Track/Timing 파일 생성,
DXRT NPU profiler 평균 inference 13.769 ms를 확인했다. 상세 실행 명령·산출물·
제한은 [`TEST_RESULTS.md`](TEST_RESULTS.md)에 있다.

호스트 변경은 아직 미커밋·미배포이므로 이 결과는 새 Tracker 정책의 실기 검증이
아니다. 원격의 브랜치, SHA, 기존 미추적 파일, Python 서버·짐벌 브리지·DXRT
상태는 전후 동일했다. 다음 단계는 호스트 Benchmark/Runtime 구현과 테스트,
Commit/Push 이후 격리된 원격 테스트 및 복원이다.

## 2026-11-11 통합 커밋 Benchmark 검증

호스트 작업을 `6f582eab2d311bdaa02d1f5015965188ace879e9`로 Commit/Push했다.
Orange Pi의 기존 작업 트리를 유지하고 격리된 worktree에서 이 SHA의 DXRT
빌드와 `frame_policy` CTest를 통과했다. 같은 `seq_004`의 네 Tracker/정책
구성이 모두 281프레임을 처리하고 정상 종료했다. [실기 결과와 로그](test_evidence/orangepi_integrated_6f582ea/)에
종료 코드, 단계별 지연, 출력 차이 및 평가 한계를 기록했다.

테스트 worktree를 제거하고 기존 원격 브랜치·HEAD·미추적 파일·Python/짐벌/DXRT
상태의 복원을 확인했다. GMC 옵션은 13.938 ms의 평균 Tracking 비용이 측정돼
성능 개선·재검증이 필요하다. 사후 ReID는 이 시퀀스에서 raw Track과 결과가
동일해 ID 개선을 주장하지 않는다. Phase 1의 Gate/Judge/Motion 및 실제
ID 평가, Phase 2 Runtime 전체 기능, Phase 3 PC GUI, Phase 4 LAN은 남았다.
