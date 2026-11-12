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
  실제 Runtime 실행은 수행하지 않았다. Orange Pi에서는 빌드만 별도 확인한다.
