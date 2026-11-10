# JPEG / H.264 송신과 간단한 비교

Orange Pi의 `feat/gui-video` 작업공간에서 빌드한다. MPP 플러그인이 없으면 송신은 실패하며 소프트웨어 인코더로 자동 전환하지 않는다.

```bash
cmake -S . -B /home/orangepi/.cache/lwir-runtime/build -DLWIR_TEST_MPP=ON
cmake --build /home/orangepi/.cache/lwir-runtime/build --parallel 4
ctest --test-dir /home/orangepi/.cache/lwir-runtime/build -R 'app_config|gui_sender' --output-on-failure
```

실제 runtime은 `config/runtime.yaml`의 `gui.enabled`, 영상/metadata 목적지 IP를 설정한다. 영상 선택은 다음 두 조합이다.

| 설정 | JPEG | H.264 |
|---|---|---|
| `gui.video.codec` | `jpeg` | `h264` |
| `gui.video.encoder` | `mppjpegenc` | `mpph264enc` |
| 품질 조건 | `jpeg_quality: 80` | `bitrate_kbps: 3000`, `keyframe_interval: 30` |
| RTP payload type | 26 | 96 |

영상 UDP 5000, metadata UDP 5001, 명령 TCP 5002로 Notion의 포트 배치에 맞췄다. 이전 runtime의 metadata 5002 / 명령 5001과 바뀌었으므로 기존 외부 클라이언트가 있으면 함께 맞춘다. PC의 현재 영상 비교 기능은 metadata 수신·검출 박스 동기화 기능을 추가하지 않는다.

두 코덱 모두 RAW16→Gray8→BGR을 거쳐 NV12로 변환하며, 인코딩 전 큐와 appsrc는 최신 프레임을 유지한다. H.264는 baseline, CBR, 설정한 GOP, IDR마다 헤더 전송을 사용한다. `jpegenc`/`x264enc`는 개발용으로 명시했을 때만 허용한다.

## 같은 입력으로 비교

`lwir_video_send`는 NPU 없이 실제 송신부를 사용한다. 입력은 **640×480 little-endian uint16 프레임들을 이어 붙인 헤더 없는 RAW 파일**, 또는 **640×480 CV_16UC1 PNG 파일들이 있는 디렉터리**다. RAW 한 프레임은 614400바이트다. PNG는 파일명순으로 읽는다. 입력 끝에서는 처음부터 반복하고 30Hz로 공급한다. 표시 범위는 두 코덱 모두 기본 0~65535로, 현재 모델 설정과 같다. 실제 runtime에서 범위를 바꿨다면 `--clip-min` / `--clip-max`도 동일하게 지정한다.

PC에서 같은 코덱을 선택한 뒤, 보드를 이더넷으로 연결해 각각 실행한다.

```bash
SEND=/home/orangepi/.cache/lwir-runtime/build/lwir_video_send
$SEND --input /path/scene.raw --codec jpeg --host <PC_IP> --seconds 30 --quality 80 --output /tmp/jpeg.csv
$SEND --input /path/scene.raw --codec h264 --host <PC_IP> --seconds 30 --bitrate 3000 --gop 30 --output /tmp/h264.csv
# 촬영한 16-bit PNG 프레임 디렉터리도 입력 가능
$SEND --input /path/sequence/frames --codec jpeg --host <PC_IP> --seconds 30 --output /tmp/jpeg-png.csv
```

CSV에는 평균값 한 행만 기록한다. 프레임별 타임스탬프를 저장할 필요는 없다.

| 값 | 측정 범위 |
|---|---|
| `preprocess_ms` | RAW16→Gray8→BGR 평균 시간 |
| `encode_rtp_ms` | appsrc 투입 직전부터 해당 프레임 마지막 RTP 조각 생성까지의 평균. 큐 대기·NV12 변환·인코딩·패킷화 포함 |
| `timing_samples` | 처리시간을 측정한 출력 프레임 수. 드롭된 입력은 이 평균에서 제외 |
| `tx_fps` | RTP로 만들어진 출력 프레임/초. PC 표시 FPS와 별도 |
| `cpu_percent_one_core` | 프로세스 전체 CPU 사용률, 한 코어 100% 기준. 파일 읽기·전처리·파이프라인·초기화 포함 |
| `rtp_mbps` | RTP 헤더+페이로드의 생성량. UDP/IP/Ethernet 헤더와 metadata 제외 |
| `submitted` / `encoded` | 입력한 프레임 수 / RTP 출력 프레임 수 |

평균에는 시작 시 초기 처리도 포함한다. runtime 실행 중에도 약 1초마다 누적 평균 전처리·인코딩/RTP 시간을 로그로 볼 수 있다. PC에는 최근 약 1초의 평균 `Decode + delivery` 시간이 표시된다. PC 값은 Jitter Buffer 이후 마지막 RTP 조각부터 이미지 복사 완료까지이며 polling 대기도 포함한다. 네트워크·화면 paint 시간은 제외한다. 서로 다른 장치에서 측정한 구간의 합을 전체 화면 지연으로 해석하지 않는다.

정지 장면과 움직이는 장면을 같은 조건으로 비교한다. 두 코덱이 각각 충분한 화질이 되도록 품질/비트레이트를 조절한 뒤, 처리시간·PC FPS·보드 CPU·전송량·화면 깨짐/끊김을 보고 선택 이유를 남기면 된다. 아직 최종 코덱을 정한 것은 아니다.

`--synthetic`은 움직이는 단순 그라데이션으로 연결을 확인하는 옵션이다. 실제 열영상과 압축 특성이 다르므로 이 결과로 최종 코덱을 선택하지 않는다.
