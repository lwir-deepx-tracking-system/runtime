# lwir-runtime

## 새 세션 시작 절차

1. 이 파일과 `docs/PROJECT_PROGRESS.md`, `docs/ARCHITECTURE.md`를 먼저 읽는다.
2. `git rev-parse --show-toplevel`, `git status --short --branch`, 현재 HEAD를 확인한다.
   진행 문서의 브랜치·커밋·수정 파일과 다르면 현재 코드와 Git 상태를 우선한다.
3. PC GUI나 Python ZIP을 다루는 작업은 해당 저장소·자료가 실제로 있는지 확인한다.
   다른 저장소 또는 과거 대화의 내용을 현재 체크아웃의 구현으로 간주하지 않는다.
4. 각 작업 단계가 끝날 때 `docs/PROJECT_PROGRESS.md`에 완료 내용, 변경 파일,
   실행한 테스트와 결과, 미검증 사항, 다음 순서를 갱신한다. 아키텍처 결정이
   바뀌면 `docs/ARCHITECTURE.md`도 함께 갱신한다.

## 유지할 Runtime 구조

- `CameraThread → DetectionThread → TrackingThread → GUI / Control` 흐름을 유지한다.
- `ThreadSafeQueue`의 생산자·소비자와 최신 결과 유지 정책을 보존한다.
  Camera→Detection 입력은 최신 2개, Tracking→Control은 최신 1개,
  Tracking→GUI는 최신 2개를 유지한다.
- 비동기 Detection의 완료 순서 재정렬, `model_generation` 검사,
  `TrackerManager`의 snapshot/epoch/`publish_if_current()`를 보존한다.
- `ModelManagerThread`, `TrackerFactory`, `TrackerManager`,
  `TrackerChangeService`, `DetectorFactory`, `ReidFactory`의 책임을 유지한다.
  Tracker 교체는 후보 생성에 성공한 뒤 활성 객체를 교체하며 실패 시 기존 객체를 유지한다.
- ByteTrack 단독, ByteTrack 사후 ReID, BoT-SORT 사전 Appearance ReID의 실행
  순서는 각 Tracker 구성 객체가 담당한다. `TrackingThread`에 종류별 분기를 늘리지 않는다.
- `FrameContextPtr`의 원본 프레임 공유, 원본 영상 좌표의 Detection/Track,
  기존 GUI/Control 결과 계약을 유지한다.
- PC 연결은 기존 UDP 영상·메타데이터와 TCP 명령 경로를 확장한다.
  Python 웹서버·HTTP·MJPEG 구조를 Runtime에 복사하지 않는다.
- 짐벌 하드웨어 명령 경로는 기존 `ControlThread`/
  `GimbalCommandExecutor` 중심의 단일 경로로 유지한다.

## Project

- C++ LWIR object-detection runtime
- Current runtime YAML selects YOLOv8n; the model catalog also registers YOLO26s.
- Camera input contract: OpenCV CV_16UC1
- Target runtime: DEEPX NPU

## Architecture

- Model-specific settings are stored in config/detection/*.yaml.
- AppConfig owns all YAML parsing and validation.
- Processing classes receive typed configuration structures.
- LwirPreprocessor must not open or parse YAML files directly.
- Detection output must use original-frame pixel coordinates.

## Preprocessing

- Validate CV_16UC1 input.
- Convert configured 16-bit LWIR range to 8-bit.
- Convert single-channel input to three channels.
- Apply letterbox resize to the model input dimensions.
- Runtime preprocessing must match training/export preprocessing.

## Hardware availability

- Host development does not assume a local NPU. Orange Pi access and hardware
  availability must be verified for each test session.
- Source editing, CPU-only tests and compilation may proceed without NPU.
- Do not create fake inference outputs.
- Mark DXNN loading, inference, tensor validation and NPU benchmarks as unverified.
- Do not claim hardware inference passed without actual execution.

## Safety

- Preserve existing user changes.
- Inspect git status before editing.
- Do not run destructive reset, clean, or forced checkout commands.
- For the explicitly authorized integration task, develop on a host work branch,
  test, then commit/push that test branch. Do not merge into protected branches.

## Orange Pi SSH and test restoration

- Local-only SSH configuration: `.local/orangepi-ssh.json` (mode `600`).
  The entire `.local/` directory is excluded by `.gitignore`. Never print its
  contents, stage it with `git add -f`, include it in artifacts, or upload it.
- Prefer SSH keys. When password authentication is required, the local helper
  `python3 .local/orangepi_ssh.py '<remote command>'` reads this configuration
  and supplies the password only to SSH's non-echoing interactive prompt.
  Do not put credentials in command arguments, environment variables or logs.
- Before remote tests record the actual repository path, branch, HEAD, dirty
  and untracked files, running processes, device owners and service state.
- Develop sources on the host. Compare deployed and host commit SHAs. Preserve
  remote modifications with an isolated worktree when necessary.
- Protect remote test cleanup against failures and signals. Stop only test-owned
  processes, retain logs separately, restore the original branch/HEAD and original
  service state, then verify working tree and processes. Never report restoration
  completed without evidence; a forced session termination may require recovery.
