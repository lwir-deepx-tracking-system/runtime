# 통합 작업 검증 기록

## 2026-11-11 접근성 및 로컬 인증

| 검증 | 결과 |
| --- | --- |
| Runtime Git 상태/HEAD, `feature/reid` 비교 | `porting`, `62389783850f3f62244fcec4780d89cfd156a2f8`; 기준 브랜치와 같은 SHA |
| `git ls-remote` 및 PC 저장소 clone | 성공; PC HEAD `8e91e3eb562a56f9fad159fdf69aec6603d2beb0` |
| Python ZIP 목록/추출 | 성공; `/tmp/lwir-python-reference-20261111` |
| `ssh -o BatchMode=yes` | 키 인증 거부 |
| 로컬 helper를 이용한 SSH | 성공; 지정 호스트, 사용자 `orangepi`, `aarch64` 확인 |
| `git check-ignore -v .local/orangepi-ssh.json .local/orangepi_ssh.py` | 두 파일 모두 `/.local/` 규칙으로 제외 |
| `stat` 및 `git ls-files .local` | 디렉터리 `700`, 인증 파일 `600`, 추적된 로컬 파일 없음 |
| 호스트 모델 SHA-256 비교 | Manifest와 일치하는 7개 모델 발견; 전체 모델 계약 검증은 진행 중 |
| 원격 `parse_model -m …/yolo26m_flir_whole480_nms_640x480.dxnn -v` | 메타데이터 조회 성공; 추론은 실행하지 않음 |
| 이번 단계 CMake/CTest/NPU/카메라/LAN 실행 | 미실행; 이번 단계는 접근성과 인증 설정 확인 |

호스트 Python은 `hashlib.file_digest`를 제공하지 않아 첫 해시 조회가 실패했다.
파일을 청크 단위로 읽는 `hashlib.sha256().update()` 방식으로 재실행해 성공했다.

## Orange Pi 사전 상태

- 저장소: `/home/orangepi/workspace/runtime`
- 원래 브랜치: `feat/runtime-telemetry`
- 원래 HEAD: `71eb9c84922723176ca8312a5b87bddff82d2387`
- 작업 트리: `?? tests/tracking/lwir_to_mot.py`
- `dxrt.service`: active, `/dev/dxrt0` 존재
- 기존 짐벌 브리지 PID: `30416`; 최초 조회의 `cam_pipe` PID `59921`은 후속 조회에 없었음.
- 테스트 브랜치/SHA: 아직 없음. 원격 브랜치 전환 및 서비스 변경 없음.
- 복원: 아직 테스트 전환을 하지 않았으므로 복원 작업 대상 없음.
  이후 실제 테스트에서는 새 사전 상태를 기록하고 종료 후 반드시 대조해야 한다.
- 조사 종료 후 재확인: 원래 브랜치·HEAD·미추적 파일 목록 동일,
  `dxrt.service` active, 기존 짐벌 브리지 PID `30416` 유지.
- 호스트 `git diff --check`: 통과.

인증 파일 내용, 비밀번호 및 SSH 전송 입력은 결과 로그에 포함하지 않는다.

## 2026-11-11 Orange Pi 기존 Benchmark 실기 실행

- 대상: Orange Pi의 **기존** `feat/runtime-telemetry` / `71eb9c84922723176ca8312a5b87bddff82d2387`.
  호스트의 미커밋 `feat/python-runtime-integration` 변경은 배포하지 않았다.
- 원본 `config/benchmark.yaml`을 `/tmp/lwir-benchmark-check-20261111/benchmark.yaml`에
  복사해 `seq_004` 한 시퀀스 및 시험 출력 경로만 지정했다. 원본 설정과 소스는 변경하지 않았다.
- 실행: 저장소 루트에서 `timeout 240s ./build/benchmark/lwir_benchmark
  /tmp/lwir-benchmark-check-20261111/benchmark.yaml`. 종료 코드 **0**.
- 결과: **281 프레임** 처리, `yolo26s` + ByteTrack. Detection 10044행,
  Track 2709행, timing CSV 281개 프레임 데이터. 실제 DXRT NPU Inference가
  profiler에 기록됐다. 평균 NPU Inference **13.769 ms**, Postprocess
  **6.951 ms**, Tracking **0.221 ms**, Total Latency **28.576 ms**.
- 당시 기존 Python 추적 서버가 NPU를 사용 중이었다. 이 수치는 동시 실행 조건의
  단일 시험 결과이며 독립 처리량 또는 새 구현의 성능 비교로 해석하지 않는다.
- 원격 산출물: `/tmp/lwir-benchmark-check-20261111/results/yolo26s_bytetrack/20261111_221635/`.
  호스트에 [로그와 CSV](test_evidence/orangepi_seq004_20261111/)를 수집했다.
  원격에는 Detection/Track 출력 원본도 남겨 두었다.
- 종료 후 원래 브랜치·HEAD와 `?? tests/tracking/lwir_to_mot.py`가 동일했다.
  Python 서버 PID 30367, 짐벌 브리지 PID 30416, `dxrt.service` active 유지.
  Benchmark 프로세스는 종료했다. 이번 실행에서 브랜치 전환이 없었으므로
  원래 브랜치가 유지된 것을 확인했으며, 테스트 브랜치 복원 절차는 수행하지 않았다.
- 미검증: 호스트 개발 브랜치의 새 정책 코드, ReID 및 BoT-SORT 실기 Benchmark,
  IDSW/IDF1/HOTA, DXNN 추가 모델 로딩, PC/LAN 통합.

## 2026-11-11 Phase 1 CPU 검증

```sh
cmake -S . -B /tmp/lwir-integration-build -DLWIR_ENABLE_DX_APP=OFF -DLWIR_ENABLE_I3_CAMERA=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/lwir-integration-build -j4
ctest --test-dir /tmp/lwir-integration-build --output-on-failure
```

- 기존 `build/` 디렉터리 쓰기 권한 때문에 첫 configure 실패. 기존 파일을 변경하지 않고
  `/tmp/lwir-integration-build`에서 configure/build 성공.
- 코드 추가 전 전체 37/37 통과, 정책 추가 후 전체 38/38 통과 (3.27초).
- 새 `frame_policy` 테스트: Warmth percentile/필터, 원본 FrameContext 공유,
  Class Gate, Edge Birth, confirmation, SORT low pass 비활성, 실제 OpenCV GMC
  이동량/리셋/해상도 변경 fallback, YAML 유효성 및 OC-SORT Factory 선택.
- NPU 모델 로딩/추론, 실제 MOT 데이터의 IDSW/IDF1/HOTA, 지연 비교, 카메라 및
  짐벌 동작, PC GUI/LAN 테스트는 미실행. CPU 테스트는 이를 대체하지 않는다.

### 후속 인덱스 및 경로 회귀 검증

- 동일 박스가 두 번 나타나는 입력에서 Warmth 필터 후 Embedding 및 BoT-SORT
  이벤트의 source index를 1, 2로 각각 복구하는 테스트 통과.
  Lost의 -1 및 PostReID stable ID 이벤트는 보존한다.
- YAML `path_base` 적용 후 Runtime/Benchmark를 `/tmp` CWD에서 로딩하여
  원래 모델/Tracker 경로와 동일함을 검증했다.
- 중간 전체 테스트 실행에서 `app_config` exit 51: 임시 YAML을 복사하는 테스트가
  상대 path_base를 새 디렉터리 기준으로 읽은 것이 원인. resolved config를 복사하도록 수정.
- 같은 실행의 `benchmark_config`는 빌드 링크 완료 전에 테스트를 시작해 permission denied로
  미실행됐다. 빌드 종료 확인 후 다시 실행했다.
- 수정 후 전체 빌드 완료, **38/38 통과 (3.27초)**. `git diff --check` 통과.
- 이번 후속 작업도 원격 브랜치/서비스 변경 및 실기 테스트는 하지 않았다.

## 2026-11-11 호스트 통합 커밋의 Orange Pi 실기 Benchmark

- 호스트 브랜치 `feat/python-runtime-integration`의
  `6f582eab2d311bdaa02d1f5015965188ace879e9`를 Push하고 원격 SHA를 대조했다.
- Orange Pi 기존 `feat/runtime-telemetry` / `71eb9c84922723176ca8312a5b87bddff82d2387`와
  미추적 `tests/tracking/lwir_to_mot.py`를 기록했다. 기존 Python 서버와 짐벌
  브리지는 실행 중이었다.
- 원격 별도 worktree에 테스트 커밋을 배치해 CMake DXRT 활성 빌드 성공.
  `frame_policy` CTest 1/1 통과. 첫 SSH 장시간 무출력 타임아웃으로 Benchmark 전
  스크립트가 끝났으나 보호 절차가 worktree를 제거했다. SSH helper를 로컬에서
  수정한 뒤 같은 빌드 산출물과 새 worktree로 시험을 완료했다.
- 네 구성(ByteTrack, ByteTrack 사후 ReID, BoT-SORT 사전 ReID, Warmth/Edge/GMC
  사후 ReID) 각각 `seq_004` 281프레임, 종료 0, DXRT 실제 추론 및 출력 확인.
  설정·수치·제한과 로그는 [증거](test_evidence/orangepi_integrated_6f582ea/)에 있다.
- 네 실행의 Detection 파일 SHA가 같았고, 사후 ReID의 Track 결과는 raw와
  동일했다. BoT-SORT에서는 appearance_gate 이벤트 10건. 새 GMC 정책의
  Tracking 평균 지연은 13.938 ms로 비용이 컸다.
- 후처리로 별도 worktree 제거. 원래 브랜치·SHA·미추적 파일 및 기존 Python
  PID 30367·짐벌 PID 30416·DXRT active가 전후 동일했다. 테스트 프로세스 종료.
- TrackEval 미설치 및 GT 출처 미확인으로 IDSW/IDF1/HOTA는 미평가.
  Runtime GUI/LAN, 모델 교체, PC 애플리케이션은 이번 테스트 범위가 아니다.

### 원격 상태 재확인

읽기 전용 SSH 재조회 결과: `feat/runtime-telemetry`,
`71eb9c84922723176ca8312a5b87bddff82d2387`,
`?? tests/tracking/lwir_to_mot.py`가 사전 기록과 동일하다.
`dxrt.service` active, 기존 `gimbal_bridge_v` PID 30416 유지.
이번 작업에서는 원격 테스트 브랜치 전환·서비스 변경을 하지 않았다.
이 확인은 실기 테스트 후 복원 검증을 대신하지 않는다.

## 2026-11-11 동일 브랜치의 기존 Runtime checkout에서 Benchmark 검증

- 호스트 브랜치/HEAD: `feat/python-runtime-integration` /
  `bef2e5471451d3ed298d554cb60d76fc78fdeea4`; 로컬 변경 5개 파일은
  미커밋 상태. `config/benchmark.yaml`의 기본 Tracker id 4와 `seq_004`를
  설정 테스트로 확인했다. 호스트 전체 CTest **38/38 통과**.
- Orange Pi 사전 상태: `/home/orangepi/workspace/runtime`,
  `feat/runtime-telemetry` / `71eb9c84922723176ca8312a5b87bddff82d2387`,
  `?? tests/tracking/lwir_to_mot.py`, Python 서버 PID 30367, 짐벌 PID 30416,
  DXRT 서비스 active. 기존 작업 파일과 프로세스를 변경하지 않았다.
- 기존 checkout을 호스트와 동일한 `feat/python-runtime-integration` /
  `bef2e5471451d3ed298d554cb60d76fc78fdeea4`로 `git switch`하고,
  호스트에서 수정한 설정/CMake/테스트/README를 전송했다.
  `./scripts/build_benchmark.sh` 빌드 성공;
  `./build/benchmark/lwir_benchmark config/benchmark.yaml` 종료 **0**.
  실제 DXNN/DXRT에서 `seq_004` **281프레임** 처리.
- 표준 결과:
  `/mnt/lwir_data/results/benchmark/yolo26s_bytetrack/20261111_224205/`.
  `timing/seq_004.csv` 285줄(헤더·푸터 포함),
  `timing/seq_004_profiler_summary.txt`, `detections/seq_004.txt`,
  `tracks/seq_004.txt` 1512줄, 실행 YAML 사본 확인.
  터미널에도 같은 DXRT profiler와 `FRAME STAGES` 표가 출력됐다.

| 단계 | 평균 ms | P95 ms |
| --- | ---: | ---: |
| Detection | 28.556 | 38.906 |
| Tracking (GMC 포함) | 13.896 | 18.232 |
| ReID | 5.178 | 6.498 |
| Matching | 0.032 | 0.043 |
| Frame total | 48.023 | 60.107 |

- 동시 실행 중인 Python 추적 서버가 NPU를 사용하므로, 이 단일 실행만으로
  독립 실행 성능이나 알고리즘 우열을 판단하지 않는다.
- 테스트에서 전송한 네 파일만 원격 브랜치 상태로 복원하고
  `feat/runtime-telemetry` / `71eb9c84922723176ca8312a5b87bddff82d2387`로
  전환했다. 미추적 파일, Python/짐벌 프로세스, `dxrt.service active` 확인.
  이미 만든 재시험용 worktree는 삭제하지 않았다.

## 2026-11-12 Runtime 카탈로그 호스트 검증

- `cmake -S . -B /tmp/lwir-runtime-host -DCMAKE_BUILD_TYPE=Debug
  -DBUILD_TESTING=ON -DLWIR_BUILD_RUNTIME=ON -DLWIR_BUILD_BENCHMARK=OFF
  -DLWIR_BUILD_CAPTURE=OFF -DLWIR_ENABLE_I3_CAMERA=OFF
  -DLWIR_ENABLE_DX_APP=OFF` 성공. `cmake --build /tmp/lwir-runtime-host -j 4`에서
  `lwir_runtime`과 테스트 대상 빌드 성공.
- `ctest --test-dir /tmp/lwir-runtime-host --output-on-failure`: 최종
  **38/38 통과**. 중간 실행에서 기존 `multitracker_config`의 카탈로그 크기 3
  가정으로 1개 실패했고, 새 ID 4~6 검증으로 갱신한 후 전체 재실행했다.
- 이 검증은 호스트 CPU/Stub 빌드다. 카메라, 짐벌, DXNN 로딩과 NPU 추론,
  실제 Runtime 실행은 수행하지 않았다.

### Orange Pi 빌드 전용 검증

- 호스트 커밋/원격 테스트 SHA:
  `885f99acc105acd9661a73a05ca6b2fd499fbdd8`.
  기존 `/home/orangepi/workspace/runtime`를 같은 브랜치/SHA로 전환해
  `cmake -G Ninja -S /home/orangepi/workspace/runtime
  -B /tmp/lwir-runtime-build-20261112 -DCMAKE_BUILD_TYPE=Release
  -DBUILD_TESTING=OFF -DLWIR_BUILD_RUNTIME=ON -DLWIR_BUILD_BENCHMARK=OFF
  -DLWIR_BUILD_CAPTURE=OFF -DLWIR_ENABLE_I3_CAMERA=ON
  -DLWIR_ENABLE_DX_APP=ON`, 이어 `cmake --build
  /tmp/lwir-runtime-build-20261112 --target lwir_runtime --parallel 2` 실행.
  컴파일·링크 **52/52 성공**, 실행 가능한 바이너리 확인.
- `lwir_runtime`을 **실행하지 않았으므로** 카메라 open, DXNN 로딩/추론,
  짐벌·GUI/LAN 동작과 FPS는 미검증이다. 기존 카메라 관련 프로그램을 중지하지 않았다.
- 원래 checkout은 `feat/runtime-telemetry` /
  `71eb9c84922723176ca8312a5b87bddff82d2387`로 복원했고,
  사전 미추적 `tests/tracking/lwir_to_mot.py`와 동일한 작업 트리,
  Python 추적 서버·짐벌 실행 및 `dxrt.service active`를 확인했다.

## 2026-11-12 PC GUI Tracker/HOME 진단 검증

- PC 소스: `/workspace/lwir-deepx-tracking-system/pc-application`, 브랜치
  `feat/python-runtime-integration`, 커밋 `a4fa282` (Push 완료).
- 호스트 정식 의존성은 미달(CMake 3.22.1, Qt 6.2.4,
  GStreamer 1.20.3). `/tmp/lwir-pc-source` 사본에서만 CMake 4.3.1을
  사용하고 GStreamer 최소 버전·yaml-cpp 최소 버전·Qt setup 명령을
  호스트 호환 값으로 대체했다. 이 빌드는 **정식 요구 환경 검증이 아니다**.
- 진단 빌드 `cmake --build /tmp/lwir-pc-compat-build --parallel 4` 성공.
  `QT_QPA_PLATFORM=offscreen ctest --test-dir /tmp/lwir-pc-compat-build
  -R '^control$' --output-on-failure`는 **1/1 통과**. 새 Tracker 조회,
  변경 완료·실패·타임아웃·재접속과 HOME 버튼 Mock 사례 포함.
- 전체 CTest는 **1/4 대상 통과**. `runtime-telemetry`/`performance`는
  호스트 `powershell` 부재, `video-integration`은 기존 Linux SIGTERM
  종료 코드 가정 4건과 GStreamer 1.20 JPEG 수신 사례 2건이 실패했다.
  영상 테스트의 H.264 사례는 통과했고 보드 연결 사례 2건은 건너뛰었다.
- PC GUI 검증 중에는 Orange Pi를 사용하지 않았다. 뒤이어 Runtime
  빌드 스크립트만 원격 실행했으며 카메라/DXRT/LAN 시험은 하지 않았다.

### Orange Pi의 기존 Runtime 빌드 스크립트 확인

- 사전 기본 경로 `/home/orangepi/workspace/runtime`:
  `feat/runtime-telemetry` / `71eb9c84922723176ca8312a5b87bddff82d2387`,
  기존 미추적 `tests/tracking/lwir_to_mot.py`.
  보존 worktree `/home/orangepi/workspace/runtime-integration-test`:
  `feat/python-runtime-integration` / `87ddb436acb28410e7789ad861836882aa37aad9`, 깨끗함.
- 같은 브랜치를 두 worktree에 동시에 Checkout할 수 없어 보존 worktree를
  잠시 detach하고 기본 경로를 호스트 브랜치/SHA로 전환했다.
  `BUILD_DIR=/tmp/lwir-runtime-build-20261112 CMAKE_BUILD_PARALLEL_LEVEL=2
  ./scripts/build_runtime.sh`를 실행했다. 로그
  `/tmp/lwir-runtime-build-script-20261112.log`에 Ninja **40/40** 링크 완료,
  실행 가능한 `lwir_runtime` 파일을 확인했다. `SCRIPT_BUILD_OK`까지 출력했다.
- 원격 래퍼의 종료 코드는 **1**이어서 전체 원격 절차를 exit 0으로 보고하지
  않는다. 별도 읽기 전용 SSH에서 기본 경로의 원래 브랜치/SHA/미추적 상태,
  보존 worktree의 브랜치/SHA/깨끗한 상태, 바이너리를 재확인했다.
  래퍼 종료 코드 원인은 미확인이다.
- 이 검증은 컴파일/링크만 포함한다. 카메라, 짐벌, DXNN 로딩·추론,
  PC LAN 연결은 실행하지 않았고 기존 카메라 소유 프로그램을 정지하지 않았다.
  사후 SSH에서 Python tracking server PID 22887, cam_pipe PID 22914,
  gimbal bridge PID 22921 및 `dxrt.service active`를 확인했다.

## 2026-11-12 Orange Pi 실제 카메라 Runtime 시험

- 시험 직전 기본 경로: `/home/orangepi/workspace/runtime`,
  `feat/runtime-telemetry` / `71eb9c84922723176ca8312a5b87bddff82d2387`,
  기존 `?? tests/tracking/lwir_to_mot.py` 보존.
  보존 worktree: `/home/orangepi/workspace/runtime-integration-test`,
  `feat/python-runtime-integration` / `87ddb436acb28410e7789ad861836882aa37aad9`, 깨끗함.
- 시험 직전 Python tracking server, `cam_pipe`, gimbal bridge는 모두 중지 상태였다.
  USB 카메라 점유 PID 없음. 기존 `~/lwir_keepalive.pause`는 사용자가 다른
  작업자를 위해 만들어 둔 파일이며, 크기·수정 시각을 보존했다.
- 두 시험 모두 보존 worktree를 잠시 detach하고 기본 경로를 호스트와 같은
  SHA로 전환했다. `cmake --build /tmp/lwir-runtime-build-20261112
  --target lwir_runtime -j2`에서 매번 Ninja 40/40 링크 성공.
  `/tmp/lwir-runtime-build-20261112/lwir_runtime <임시 YAML>`을
  `timeout --preserve-status -s INT -k 20s 30s`로 실행했다.
  임시 YAML은 `control.enabled=false`, `gui.enabled=false`, 별도 `/tmp`
  측정 출력 경로를 사용했다. 실제 짐벌 이동과 LAN 통신은 수행하지 않았다.

| 구성 | 종료 코드 | Detection 프레임 | NPU processing 평균 | Detection latency 평균 | Tracking 처리 평균 | Tracking E2E 평균 | 결과 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| ID 1 ByteTrack | 0 | 651 | 3.805 ms | 20.353 ms | 0.040 ms | 39.343 ms | `/tmp/lwir-runtime-camera-test-20261112-091533/results/core/20261112_091639/` |
| ID 4 Filter/GMC/Post-ReID | 0 | 648 | 3.805 ms | 19.063 ms | 10.399 ms | 48.438 ms | `/tmp/lwir-runtime-camera-test-20261112-091901/results/core/20261112_092007/` |

- ID 1 Detection `detection_timing.csv` 651행, `metrics.csv` 1953행.
  ID 4는 각각 648행, 1944행. 각 실행에서 Camera/Detection/Tracking
  단계가 프레임 수만큼 기록됐고 카메라 닫기 및 측정 저장 로그가 있다.
- ID 4 시험에 쓰인 RepVGG 파일은
  `/home/orangepi/workspace/model-bank/tracking_server/models/reid/repvgg_tmot_camel_reid.dxnn`,
  SHA-256 `dc19d7862a77ffa1d7c7f71f84283e86c33267d9e741b59070b839cb1921ff53`.
  `parse_model -m ... -v`에서 입력 `images [1,128,64,3] uint8`, 출력
  `embedding [1,128] float32`를 확인했다. 현재 저장소 YAML의
  `../model-bank/final_tracker/tracker/models/RVGGA0_REID2C256.dxnn`은
  **현재 보드에 없음**. 따라서 ID 4는 같은 SHA 모델을 가리키는 별도
  `/tmp/.../reid.yaml`을 사용했다. ReID 모델 생성은 통과했지만, 이
  카메라 장면에서 실제 crop 및 Embedding 추론 호출 여부는 계측되지 않았다.
- 두 실행의 정리 후 독립 SSH 확인: 기본 경로는 사전 브랜치/SHA/미추적
  파일 그대로, 보존 worktree는 테스트 브랜치/SHA로 복귀하고 깨끗함.
  keepalive pause 크기·수정 시각 동일, Python/cam_pipe/gimbal 프로세스는
  사전처럼 중지 상태, `dxrt.service active`. 시험 프로세스 없음.
- 이번 시험은 현재 기본 Detection 모델 YOLOv8n의 실제 카메라 추론이다.
  Tracker 변경 TCP 명령, ReID 실제 매칭, GUI·LAN, 짐벌 동작은 검증하지 않았다.

## 2026-11-12 카탈로그·디자인 작업 검증

| 명령/확인 | 결과 | 범위 |
| --- | --- | --- |
| `python3 scripts/import_model_bank.py --source /tmp/lwir-model-bank-current` | 첫 실행 18개 복사, 재실행 18개 SHA 일치로 건너뜀 | 호스트 Runtime `models/imported/`, Manifest 전체 해시 |
| 보드 `/tmp/lwir-import-model-bank.py --source /home/orangepi/workspace/model-bank/tracking_server/models --target /home/orangepi/workspace/runtime/models/imported` | 18개 복사, 약 219 MB | 파일 배치만; 소스·서비스 전환 없음 |
| 보드 `parse_model -m <각 DXNN> -v` | 18/18 메타데이터 파싱 성공 | 실제 DXRT 로딩/추론은 포함하지 않음 |
| `cmake --build /tmp/lwir-integration-build -j4 --target lwir_runtime test_app_config test_gui_pipeline_commands` | 성공 | 호스트 CPU / DXRT 비활성 구성 |
| `ctest --test-dir /tmp/lwir-integration-build -R 'app_config\|gui_pipeline_commands' --output-on-failure` | 2/2 통과 | YAML과 TCP 카탈로그 wire 계약 |
| `/tmp` PC 소스 사본의 호환성 CMake 빌드 | `pc-application`, `control-test` 링크 성공 | 호스트 Qt 6.2/GStreamer 1.20/yaml-cpp 0.7에 맞춘 **진단 빌드** |
| `QT_QPA_PLATFORM=offscreen ctest --test-dir /tmp/lwir-pc-compat-build -R '^control$' --output-on-failure` | 1/1 통과 | 로컬 TCP, 카탈로그 이름/활성/설치 상태, 기존 제어 회귀 |
| `model-gui.png` 시각 확인 | 어두운 영상 영역, 회색 패널, 주황 AUTO 확인 | `/tmp/lwir-pc-compat-build/model-gui.png`, 실제 LAN 프레임 아님 |

첫 Qt CTest는 네트워크 샌드박스 때문에 `QTcpServer::listen()`이 실패했다.
동일 바이너리를 로컬 소켓 허용 환경에서 재실행해 1/1 통과했다. DXNN
카탈로그 새 모델은 실제 보드 로딩·추론 시험 전이며, 복사·메타데이터 검사만
완료했다. 이번 단계에서 보드 Runtime의 브랜치는 전환하지 않았다. 사후
`feat/runtime-telemetry` / `71eb9c84922723176ca8312a5b87bddff82d2387`,
기존 미추적 파일 1개를 확인했다.
# 2026-11-12 최신 model-bank 시간 기준 확인 정책 (호스트)

- 구성: CMake Debug, `LWIR_ENABLE_DX_APP=OFF`, Runtime/Benchmark 실행 파일 비활성,
  CPU 단위 테스트 활성. 빌드 위치 `/tmp/lwir-runtime-confirm-debug`.
- `cmake --build /tmp/lwir-runtime-confirm-debug --target test_frame_policy -j4` 성공.
  `ctest --test-dir /tmp/lwir-runtime-confirm-debug -R '^frame_policy$' --output-on-failure`
  1/1 통과. 130 ms 기준의 30 FPS 4프레임 확인을 ByteTrack/BoT-SORT 모두 시험했다.
- 전체 Debug CPU 테스트 빌드 성공. `ctest -j4`는 32/38 통과, 소켓 bind가
  제한된 환경에서 6개 실패. 실패 6개만 포트 사용이 가능한 환경에서 재실행해
  6/6 통과 (`auto_control_e2e`, `gui_receiver`, `gui_sender`,
  `gui_sender_jpeg`, `gui_sender_bypass`, `gui_sender_bypass_jpeg`).
- 이전 uncommitted 카탈로그 필드 변경으로 `test_multitracker_runtime.cpp`가
  전체 빌드에서 컴파일되지 않아 표시 이름 인자를 추가했다. 이후 전체 빌드 성공.
- NPU/DXRT 로딩, 실카메라 FPS에서 시간 정책, 원본 Python GUI, PC↔Orange Pi
  LAN은 이번 작업에서 검증하지 않았다. Orange Pi Git/서비스 상태도 변경하지 않았다.

## 2026-11-12 정책 조회 및 PC Python 데스크톱 브리지 (호스트)

| 실행 명령 | 결과 | 검증 범위 |
| --- | --- | --- |
| `cmake -S . -B /tmp/lwir-runtime-desktop-build -DCMAKE_BUILD_TYPE=Debug -DLWIR_ENABLE_DX_APP=OFF -DLWIR_BUILD_RUNTIME=ON -DLWIR_BUILD_BENCHMARK=ON -DLWIR_BUILD_CAPTURE=OFF -DBUILD_TESTING=OFF` 후 `cmake --build /tmp/lwir-runtime-desktop-build --target lwir_runtime lwir_benchmark -j4` | 두 실행 파일 링크 성공 | 호스트 CPU/stub 빌드, DXRT 및 I3 카메라 제외 |
| `ctest --test-dir /tmp/lwir-runtime-confirm-debug -R '^(gui_pipeline_commands|app_config|frame_policy)$' --output-on-failure` | 3/3 통과 | `0x87` wire payload, YAML, 시간 확인 정책 |
| `ctest --test-dir /tmp/lwir-runtime-confirm-debug -E '^(auto_control_e2e|gui_receiver|gui_sender|gui_sender_jpeg|gui_sender_bypass|gui_sender_bypass_jpeg)$' --output-on-failure` | 32/32 통과 | 호스트 Debug CPU 비소켓 전체 테스트 |
| 위 제외 6개만 `ctest -R`로 포트 사용 허용 환경에서 실행 | 6/6 통과 | TCP/UDP 회귀 |
| PC `PYTHONPATH=python_gui python3 -m unittest -v python_gui/test_lan_protocol.py python_gui/test_desktop_routes.py` | 10/10 통과 | Python TCP/UDP 파서, 카탈로그, 정책, 라우트, 실패 반환 |
| PC `python3 page/build_page.py --check` | 121179바이트, 31개 원본 조각과 일치 | 원본 model-bank HTML/CSS/JS 보존 |
| PC `PYTHONDONTWRITEBYTECODE=1 timeout 5s python3 python_gui/desktop_gui.py --board 127.0.0.1 --video-port 55000 --metadata-port 55001 --command-port 55002` | 5초 후 timeout 종료, Python 예외 없음 | Gtk/WebKit2/GStreamer 창 시작만 확인, Runtime 연결 없음 |

처음 GUI smoke 실행은 샌드박스의 UDP 소켓 생성 금지로 실패했다. 포트 사용
허용 환경에서 동일한 실행을 다시 수행했다. `py_compile`을 PC 저장소에서
직접 실행한 시도는 읽기 전용 `__pycache__` 생성 때문에 실패했으나,
`/tmp` 사본 컴파일과 PC 단위 테스트의 Python import는 통과했다.
영상 디코딩, 실제 프레임 오버레이, PC↔보드 LAN, 새 명령의 보드 빌드,
DXRT/NPU 및 짐벌 동작은 검증되지 않았다. 이번 단계에서 Orange Pi에
접속하거나 브랜치·서비스를 변경하지 않았다.

### 같은 날 동적 Tracker 정책 변경

- `cmake --build /tmp/lwir-runtime-confirm-debug --target
  test_gui_pipeline_commands test_tracker_change_service test_tracker_hotswap -j4`:
  빌드 성공. 이어 `ctest --test-dir /tmp/lwir-runtime-confirm-debug -R
  '^(tracker_change_service|tracker_hotswap|gui_pipeline_commands)$'
  --output-on-failure`: 3/3 통과.
- `cmake --build /tmp/lwir-runtime-desktop-build --target lwir_runtime
  lwir_benchmark -j4`: 호스트 CPU 두 실행 파일 링크 성공.
- PC 복사본 `PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=python_gui python3 -m
  unittest -v python_gui/test_lan_protocol.py python_gui/test_desktop_routes.py`:
  12/12 통과. 정책 변경의 Accepted/Completed 및 필터 호출을 검증했다.
- 첫 Tracker 서비스 테스트는 기존 지연 생성 fixture의 promise를 정책
  교체에서 다시 만족시키려 해 실패했다. fixture를 첫 호출에만 지연하도록
  수정하고 재실행해 통과했다. 실제 Runtime 코드의 실패는 아니었다.
- Orange Pi 카메라는 연결되지 않았다. 현재 표시 중인 Chrome 창은 검증된
  model-bank 원본 페이지 파일이며 새 LAN 브리지 GUI가 아니다. 실장비 실행,
  영상 프레임 동기화, 정책 변경 시 DXRT 자원은 아직 검증하지 않았다.
