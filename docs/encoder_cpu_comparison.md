# 실제 Runtime의 H.264 인코더 CPU 비교

별도 송신기나 가상 추론을 실행하지 않는다. 기존 `lwir_runtime`에 카메라·실제 NPU·추적·제어·PC 송신을 기존 설정 그대로 실행하고, `--encoder-cpu`를 추가한다. 일반 실행에는 계측이 켜지지 않는다.

## 같은 실행에서 추가되는 것

- 첫 RTP 출력 프레임이 관측된 뒤 10초는 워밍업으로 제외한다.
- 그다음 60초 동안 약 1초 간격으로 실제 프로세스 전체 CPU 시간과 기존 GuiSender 카운터를 읽는다.
- `[EncoderCPU]` 행을 표준 출력에 남긴다. `final=1`인 마지막 행이 비교 결과다.
- 계측 완료 뒤 Runtime은 계속 실행한다. Ctrl+C로 정상 종료한다.
- 기존 영상·metadata·TCP 계약, 전처리·추론·추적·제어 경로는 바꾸지 않는다.

## 실행

기존 Runtime 빌드 방법을 그대로 사용한다. 보드에 이 테스트 브랜치의 소스를 반영하는 작업은 별도이며, 개발환경에서 소스를 준비했다고 보드가 갱신되지는 않는다.

기존에 실제 운용한 YAML을 `config/local/encoder-sw.yaml`, `config/local/encoder-hw.yaml`로 각각 복사한다. **두 파일의 유일한 차이**는 아래 encoder 값이어야 한다. 모델·트래커·제어·로그 수준·telemetry·기록·IP·비트레이트·GOP·FPS·MTU는 같게 유지한다. 활성 공용 YAML은 수정할 필요가 없다.

```yaml
# SW 실행: gui.video 아래
codec: h264
encoder: x264enc
# HW 실행에서는 encoder: mpph264enc
```

이 프로젝트의 실제 설정을 비교한다. x264enc는 현재 `ultrafast/zerolatency`, MPP는 `baseline/CBR`이며 SW는 I420, HW는 NV12로 변환된다. 따라서 순수 코덱 연산량이나 동일 화질의 비교가 아니라 **현재 송신 경로를 사용하는 전체 Runtime CPU 비교**다.

카메라·보드·PC를 평소처럼 연결하고, PC에서 H.264를 수신한다. 다른 Runtime이 카메라·NPU를 사용하고 있지 않은지 확인한다.

```bash
./build/runtime/lwir_runtime config/local/encoder-sw.yaml --encoder-cpu 2>&1 | tee /tmp/encoder-sw-01.log
# final=1 출력 후 Ctrl+C. 이어서 별도로 실행한다.
./build/runtime/lwir_runtime config/local/encoder-hw.yaml --encoder-cpu 2>&1 | tee /tmp/encoder-hw-01.log
```

로그 파일명은 반복마다 바꿔 이전 결과를 보존한다. `final=1`이 없으면 60초 창이 완료되지 않은 것이므로 완성된 비교 결과로 쓰지 않는다. 표본이 도착하지 않거나 출력 FPS가 0이면 먼저 실제 송신 오류를 확인한다.

```bash
rg '\[EncoderCPU\].*final=1' /tmp/encoder-sw-01.log /tmp/encoder-hw-01.log
```

SW→HW, HW→SW 순서를 번갈아 각 3회 실행한다. 정지 장면에서는 카메라를 고정하고, 동적 장면에서는 같은 동작을 반복한다. 모델·트래커·모드 전환은 계측 중 수행하지 않는다. 실시간 장면은 완전히 같을 수 없음을 기록한다. 장면·보드 전원 모드·냉각·온도·클럭 상태도 함께 기록한다.

## 읽을 값

| 필드 | 의미 |
| --- | --- |
| `elapsed_s` | 워밍업을 제외한 실제 측정 시간. 실행 루프 간격 때문에 60초를 조금 넘을 수 있음 |
| `cpu_percent_one_core` | 직전 표본 이후 프로세스 CPU. 한 코어 100%, 여러 코어 사용 시 100% 초과 가능 |
| `avg_cpu_percent_one_core` | 측정 창 전체 프로세스 CPU. 단순 표본 평균이 아니라 CPU 시간 차 / 실제 경과 시간 |
| `submitted_fps` | 직전 구간 GuiSender 입력률. 카메라 입력 FPS와 다름 |
| `tx_fps`, `avg_tx_fps` | RTP 마지막 marker에서 관측한 출력 FPS. PC 수신·표시 FPS나 UDP 전달 성공을 뜻하지 않음 |
| `submitted`, `encoded` | 측정 창 내 송신 제출·RTP 출력 수. 비동기 구간 경계가 있어 차이를 그대로 네트워크 손실로 계산하면 안 됨 |
| `final=1` | 60초 측정 창 완료. 해당 행의 평균을 비교 |

CPU는 전처리·실제 NPU 호출의 CPU 작업·추적·제어·통신·저장·GStreamer·계측을 포함한 **전체 프로세스** 사용량이다. NPU·하드웨어 인코더 자체의 사용률이나 다른 프로세스·커널 전체 부하는 아니다. FPS가 같아도 CPU 여유가 달라질 수 있다. 출력 FPS와 PC 표시도 함께 확인하고, 30fps 미달을 CPU 절감 성과로 숨기지 않는다.

이 기능은 인코딩 p95·네트워크 단방향 지연·화질 동등성·전력을 측정하지 않는다. stdout 기록과 계측 비용은 두 실행에 동일하게 들어간다. SIGQUIT은 기존 Runtime 결과 폴더를 폐기하지만 외부 `tee` 로그는 삭제하지 않는다.

## 검증 범위

개발 PC에서는 계측 산술·CLI·CPU-only 빌드와 기존 소프트웨어 시험만 검증할 수 있다. 실제 카메라·NPU·MPP 동작, DXNN 로딩·텐서·추론·벤치마크 및 실물 짐벌은 보드 실행 전까지 미검증이다. 측정 수치를 임의로 채우지 않는다.
