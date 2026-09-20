# LWIR NPU 표적 탐지·추적 시스템

LWIR 영상에서 객체를 탐지하고 Track ID를 부여합니다. 사용자가 ID를 선택하면 그 객체의 위치를 STM32에 전달해 짐벌이 따라가도록 만드는 프로젝트입니다. 현재는 PC의 `runtime-pc` 환경에서 **YOLO + ByteTrack 기준선**의 C++ 구조를 개발 중입니다.

## 파이프라인 구조

```mermaid
flowchart LR
    CameraThread --> PreprocessThread --> InferenceThread --> PostprocessThread --> TrackingThread --> TargetSelectionThread --> CommunicationThread
```

각 상자는 별도 POSIX pthread입니다. thread 사이 데이터는 `ThreadSafeQueue`로 전달되며, [Application](runtime/src/app/application.cpp)이 구성요소와 thread를 연결하고 `Tracker` 객체를 소유합니다. `TrackingThread`는 `Tracker` 인터페이스를 통해 현재 `ByteTrackTracker`를 호출합니다.

| 전달 단계 | 데이터 타입 | 담는 내용 |
|---|---|---|
| Camera → Preprocess | `Frame` | 프레임 ID, 획득 시각, 영상 |
| Preprocess → Inference | `ModelInput` | 프레임 ID, 모델 입력 |
| Inference → Postprocess | `ModelOutput` | 프레임 ID, 모델 출력 |
| Postprocess → Tracking | `DetectionResult` | 프레임 ID, `vector<Detection>` |
| Tracking → Target selection | `TrackingResult` | 프레임 ID, `vector<Track>` |
| Target selection → Communication | `TargetSelection` | 선택 ID와 일치한 Track, 또는 일치 결과 없음 |

`Detection`과 `Track`은 객체 하나를 뜻합니다. `DetectionResult`와 `TrackingResult`는 객체가 0개인 프레임도 표시할 수 있는 **한 프레임의 결과**입니다. 일반 실행과 측정 실행은 같은 데이터 타입을 사용합니다.

## 현재 기준선과 설정

현재 브랜치의 실행 조합은 **YOLOv8 + ByteTrack**입니다. 코드나 파이프라인 연결이 달라지는 조합은 별도 브랜치에서 개발하고, 같은 구현 안에서 바꿀 모델 파일·입력 조건·임계값·측정 모드는 YAML로 관리합니다. 브랜치에서 개발한 기능이 실제 코드에 연결되기 전에는 YAML 값만 바꿔 실행할 수 없습니다.

- [runtime.yaml](runtime/config/runtime.yaml): 실행 조합, 로그 레벨, 측정 모드와 각 구성요소 설정 파일 경로. 현재 Factory가 생성할 수 있는 것은 YOLOv8 CPU 전처리·후처리와 ByteTrack입니다.
- [yolov8n.yaml](runtime/config/model/yolov8n.yaml): 모델 경로 `models/yolov8n.dxnn`, 입력 크기·전처리 조건·후처리 임계값.
- [bytetrack.yaml](runtime/config/tracking/bytetrack.yaml): 추적 파라미터. 현재 ByteTrack 구현은 파일 경로만 보관하며 값을 적용하지 않습니다.

컴파일된 `.dxnn`은 `runtime/models/`에 두도록 경로가 잡혀 있지만, 현재 모델 파일은 없고 `runtime/models/`는 Git에서 제외됩니다. `runtime.yaml`의 `communication.enabled`는 아직 통신 thread를 끄지 않습니다.

## Docker 환경과 사용 방법

`runtime-pc`는 팀원이 같은 Linux 빌드 환경에서 C++ runtime을 개발하기 위한 컨테이너입니다. 소스는 호스트 저장소를 컨테이너에 연결하고, 빌드 결과는 Docker 볼륨 `runtime-build`에 보관합니다. `compiler-dx`는 모델 컴파일용, `runtime-rpi`는 향후 Raspberry Pi 실행용으로 구분하며, 현재 Compose에는 `runtime-pc`만 정의되어 있습니다.

### 1. DEEPX 기반 이미지 준비

`runtime-pc/Dockerfile`은 고정된 DEEPX 기반 이미지 태그 `dx-runtime:dxas-5749ab70-ubuntu22.04`를 사용합니다. 팀의 DX-AllSuite 소스 버전은 **커밋 `5749ab70cc75cede356c57b9cc7cf91229e47742`**로 고정합니다. 처음 환경을 만드는 PC에서는 [DEEPX DX-AllSuite 설치 안내](https://github.com/DEEPX-AI/dx-all-suite/blob/main/docs/source/02_Setting_Up_Environment.md)에 따라 저장소를 받은 뒤, 아래 순서로 해당 커밋과 하위 모듈을 맞춰 빌드합니다.

```bash
git clone https://github.com/DEEPX-AI/dx-all-suite.git
cd dx-all-suite
git checkout 5749ab70cc75cede356c57b9cc7cf91229e47742
git submodule update --init --recursive
./docker_build.sh --target=dx-runtime --ubuntu_version=22.04
docker tag dx-runtime:ubuntu-22.04 dx-runtime:dxas-5749ab70-ubuntu22.04
docker image inspect dx-runtime:dxas-5749ab70-ubuntu22.04
```

이미 **고정 태그의 기반 이미지**가 있으면 이 단계를 건너뜁니다. 빌드 스크립트는 DX-AllSuite의 하위 모듈과 의존 파일을 사용하는 Bash 스크립트입니다. Ubuntu에서는 준비된 기반 이미지로 프로젝트 이미지를 빌드·실행했지만, DX-AllSuite를 처음부터 새로 빌드하는 1번 전체 절차와 Windows PowerShell에서의 실행은 검증하지 않았습니다.

### 2. 프로젝트 이미지 준비

이 저장소의 `compose.yaml`에는 `image: lwir-runtime-pc:dxas-5749ab70-ubuntu22.04`만 있고 자동 이미지 빌드 설정은 없습니다. 이 이미지가 없으면 **이 저장소 루트**에서 같은 태그로 빌드합니다.

```bash
docker build -t lwir-runtime-pc:dxas-5749ab70-ubuntu22.04 -f docker/runtime-pc/Dockerfile .
```

### 3. Docker 컨테이너 열기

저장소 루트에서 실행합니다.

```bash
docker compose run --rm runtime-pc
```

컨테이너의 작업 디렉터리는 `/workspace/lwir-npu-tracking-system/runtime`입니다.

### 4. 컨테이너 안에서 빌드

```bash
cmake -S . -B build
cmake --build build
```

### 5. 컨테이너 안에서 실행

```bash
./build/lwir_runtime
```

실행 파일은 현재 작업 디렉터리의 `config/runtime.yaml`을 읽습니다. 현재 Camera는 실제 영상을 읽지 않아 `open()`이 실패하고 각 thread가 종료됩니다. 따라서 이 실행은 thread 연결·종료 흐름 확인용입니다.

### 호스트 OS와 DEEPX 상태

- **Ubuntu:** Docker Engine과 Compose가 있으면 위 방식으로 Linux 컨테이너를 사용할 수 있습니다. 이 저장소의 빌드·실행 명령은 Ubuntu 호스트에서 확인했습니다. [Docker의 Ubuntu 설치 문서](https://docs.docker.com/engine/install/ubuntu/)
- **Windows:** Docker Desktop에서 Linux 컨테이너를 실행할 수 있어야 합니다. WSL 2 외에 지원되는 Windows 환경에서는 Hyper-V 백엔드도 사용할 수 있습니다. 현재 이미지는 `linux/amd64`이며, Windows 호스트에서 README의 전체 절차를 실행한 결과는 없습니다. [Docker Desktop의 Windows 설치 문서](https://docs.docker.com/desktop/setup/install/windows-install/)
- **DEEPX:** 현재 `runtime-pc` 이미지에서 DX-RT C++ API의 컴파일·링크·버전 조회를 확인했습니다. DX-RT는 기반 `dx-runtime` 이미지에서 상속합니다. **현재 프로젝트 CMake는 DX-RT를 링크하지 않고, `Inference`도 NPU 호출을 하지 않습니다.** `compose.yaml`에도 NPU 장치 전달 설정이 없어 실제 하드웨어 실행은 확인되지 않았습니다.

### 측정 모드

[runtime.yaml](runtime/config/runtime.yaml)의 `measurement.enabled`를 `true`로 설정하면 각 worker가 프레임별 큐 대기 시간과 처리 시간을 기록합니다. Tracking과 Communication 행의 `e2e_ms`는 카메라 획득 완료부터 각각 추적 완료, `STM32Link::send()` 반환까지의 지연입니다. 현재 `send()`는 빈 구현이므로 Communication 값은 실제 STM32 전송 지연을 뜻하지 않습니다. 종료 후 `runtime/results/<모델명>_<시각>/`에 `metrics.csv`와 참조한 YAML의 사본을 저장합니다. 프로그램은 원본 YAML을 실행 중 수정하지 않습니다. 별도 측정 thread는 없으며, 기록은 stage당 최대 10,000건입니다. 현재 Camera가 프레임을 반환하지 않으므로 CSV에는 헤더만 생성됩니다.

`runtime/results/`는 Git에서 제외됩니다. 측정 모드의 CSV는 **통합 실행 결과**이며, 컴포넌트 단독 테스트의 측정값은 현재 자동 저장하지 않습니다.

## 컴포넌트 테스트 예시

[runtime/tests](runtime/tests)에는 구성요소별 작업 위치와 두 가지 예시가 있습니다. [TargetSelector 테스트](runtime/tests/target/test_target_selector.cpp)는 선택 ID의 동작을 검사하는 코드입니다. [ByteTrack 테스트](runtime/tests/tracking/test_bytetrack.cpp)는 아직 구현 전 골격이며, [프레임 목록](runtime/tests/tracking/data/frames.csv)과 [검출 예시](runtime/tests/tracking/data/detections.csv)를 넣어 두었습니다. `Detection` 필드와 ByteTrack 로직이 완성되면 CSV를 읽어 프레임 순서대로 `track()`을 호출하도록 채워야 합니다. 검출이 없는 4번 프레임도 빈 목록으로 호출해야 합니다.

현재 테스트 대상은 [CMakeLists.txt](runtime/CMakeLists.txt)에서 모두 주석 처리되어 있으므로 `ctest`로 실행되지 않습니다. 각 담당자가 자기 컴포넌트를 직접 호출해 동작을 검증하고, 단독 처리 시간이 필요하면 해당 호출 구간을 측정합니다. 통합 실행의 큐 대기·전체 지연은 위 측정 모드에서 확인합니다.

## 구현할 내용과 수정 위치

| 담당 단계 | 주요 파일 | 구현할 내용 |
|---|---|---|
| Camera | `runtime/src/camera/camera.cpp`, `runtime/include/common/frame.hpp` | LWIR 프레임 획득, 이미지 형식과 버퍼 수명 확인 |
| Preprocess | `runtime/src/preprocess/yolov8_preprocessor.cpp`, `runtime/include/common/model_input.hpp` | 모델 YAML에 맞는 영상 변환과 입력 텐서 구성 |
| Inference | `runtime/src/inference/inference.cpp`, `runtime/include/common/model_output.hpp` | DX-RT 모델 로드·실행, 출력 텐서와 소유권 정의 |
| Postprocess | `runtime/src/postprocess/yolov8_postprocessor.cpp`, `runtime/include/common/detection.hpp` | YOLO 출력 해석, 점수 필터와 NMS, Detection 생성 |
| Tracking | `runtime/src/tracking/bytetrack_tracker.cpp`, `runtime/include/common/track.hpp` | ByteTrack 상태·ID 관리, Track 생성, 설정 적용 |
| 대상 선택 | `runtime/src/target/target_selector.cpp`, `runtime/src/pipeline/target_selection_thread.cpp` | GUI 선택 ID를 현재 Track 목록에 적용하는 흐름 확인 |
| STM32 통신 | `runtime/src/communication/stm32_link.cpp`, `runtime/src/pipeline/communication_thread.cpp` | 전송 형식과 연결, 대상 없음 상태 처리 |

각 담당자는 자기 단계의 **입력 타입 → 처리 함수 → 출력 타입**을 먼저 확인하면 됩니다. `runtime/src/pipeline/*_thread.cpp`는 queue와 worker 실행 흐름을 담당합니다. 새 소스 파일을 추가하면 `runtime/CMakeLists.txt`의 빌드 대상도 확인해야 합니다.

표는 수정 파일의 전체 목록이 아닙니다. 데이터 타입이 바뀌면 `runtime/include/common/`과 관련 pipeline thread도, YAML 선택지가 늘어나면 `runtime/src/app/app_config.cpp`와 `runtime/src/factory/component_factory.cpp`도 확인해야 합니다. 새 라이브러리가 필요하면 CMake와 Dockerfile에 함께 반영합니다.

## 현재 구현 범위와 실험 방향

thread·queue 연결, 프레임 ID 전달, 대상 선택 경계, 측정 모드 저장 구조는 마련되어 있고 `runtime-pc`에서 컴파일·링크가 됩니다. 현재 활성 설정은 YOLOv8 + ByteTrack 기준선이며, 모델 경로·전처리 조건·후처리 임계값은 YAML에서 관리합니다. 실제 Camera·YOLO 추론·후처리·ByteTrack·STM32 전송은 아직 골격이고 GUI 입력도 연결되지 않았습니다. DINO·KLT와 PPU 후처리는 현재 실행 설정에 연결되어 있지 않습니다.

첫 공동 목표는 **동일한 LWIR 입력에서 YOLO 검출과 ByteTrack ID가 끝까지 전달되는 기준선**을 만드는 것입니다. 이후 모델과 처리 방식을 바꿔 성능을 비교합니다. queue 적체 정책과 측정 범위 등 팀이 합의할 항목은 [discussion.md](discussion.md)에 정리되어 있습니다.
