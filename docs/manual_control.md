# GUI 수동 방향 제어

이번 구현은 PC 키보드 → TCP MOVE → 보드 Waveshare Serial 명령까지 연결한다.
AUTO 타깃 추종과 HOME 완료 판정은 포함하지 않는다. 기존 ByteTrack·표시 경로는 유지한다.

## 책임과 설정

- AppConfig가 YAML을 파싱·검증하고 `ControlConfig` / `GimbalConfig`를 전달한다.
- `GuiCommandHandlerFactory`: `deferred`(기존 통신 시험) 또는 `manual` 전략 생성.
- `ManualCommandHandler`: 수동 상태, 입력 유효성, heartbeat, ACK·STATUS 정책의 단일 소유자.
- `IGimbalDevice` / `GimbalDeviceFactory`: 장치 교체 경계. 현재 실제 구현은 Waveshare Serial.
- SerialPort: 115200 등 설정된 baud의 raw 8N1, 비동기 FD, 전체 쓰기 deadline, FD 정리.
- TCP worker가 명령·tick·연결/해제 이벤트를 직렬 처리한다. Tracking 결과를 기다리지 않는다.
- 기존 ControlThread는 AUTO 미구현 상태로 Tracking queue를 소비만 하며 장치 I/O를 하지 않는다.

`config/runtime.yaml`의 설정 예시:

```yaml
control:
  enabled: true
  heartbeat_timeout_ms: 1500
  poll_interval_ms: 25
  gimbal:
    device: /dev/serial/by-id/<실제 CP2102N 장치 링크>
    baud_rate: 115200
    write_timeout_ms: 100
    speed: 30
    acceleration: 50
    home_x_deg: 0
    home_y_deg: 0
    x_sign: 1
    y_sign: 1

gui:
  enabled: true
  command:
    enabled: true
    handler: manual
    bind_address: 192.168.50.2
    port: 5002
    receive_timeout_ms: 1000
```

위 예시는 일부 항목만 발췌한 것이다. 영상·metadata·model 등의 기존 필수 설정도
유지한다. 공용 기본 설정은 `control.enabled: false` / `handler: deferred`를 유지한다.
둘을 함께 변경해야 실제 Serial 장치를 연다. 장치 열기/초기 STOP 제출 실패는 시작 오류로
보고하며 Runtime이 성공 상태로 계속 실행하지 않는다.

숫자 초기값은 기존 [C++ 시험 코드](https://github.com/lwir-deepx-tracking-system/third-party/blob/df57768d7205e7a394fa920216cb531ba4a921e0/waveshare-gimbal/main.cpp)를
따른다. 기존 `SerialPort`와 명령 형태를 분리·재구현하면서 constructor 예외 FD 정리와
bounded write를 추가했다. 기존 MOVE 뒤 200ms sleep/STOP은 시험 프로그램 동작이므로
이식하지 않았다. 속도·가속도의 양수 여부를 검증하며 최종 운용값은 실제 장비로 확인한다.
HOME 각도 범위는 기존 시험 코드의 X -180..180, Y -30..90을 따른다.

## 수동 입력·장치 명령

PC의 기존 방향키 입력과 `ControlClient`는 변경하지 않는다. X/Y는 각각 -1/0/1이며
0xFF는 -1로 해석한다. `x_sign` / `y_sign`을 -1로 설정하면 장치 방향을 반전할 수 있다.

| 이벤트 | 장치 명령 | TCP 결과 |
|---|---|---|
| Runtime manual 생성 | T=135 STOP 제출 | 아직 TCP 응답 없음 |
| 방향 변경 | T=141, X/Y 방향, SPD 설정값 | IDLE↔MANUAL_MOVING 변경 시 STATUS, ACK 없음 |
| 키 해제 MOVE(0,0) | T=141, X=0,Y=0 | IDLE로 변경 시 STATUS |
| 같은 방향 반복 | 추가 명령 없음 | 추가 응답 없음 |
| 범위 밖 방향 | 실행하지 않음 | ACK 없음 |
| MANUAL | 이미 MANUAL/타깃 없음, 상태 유지 | ACK OK (Serial ERROR이면 NOT_READY) |
| AUTO | 실행하지 않음 | NOT_READY, ID 0이면 INVALID_TARGET |
| HOME | 이동 중이면 T=135로 정리, T=133 HOME은 실행하지 않음 | NOT_READY 및 상태 변경 시 STATUS |
| PING | 장치 I/O 없음, 마지막 유효 PING 시각 갱신 | ACK OK |

`acceleration`, `home_x_deg`, `home_y_deg`는 어댑터의 T=133 HOME 요청 인코딩에 사용된다.
현재 GUI HOME은 피드백·완료 판단이 없어 거절하며, 가속도는 T=141 방향 명령에 보내지 않는다.
단, PC는 HOME 요청 시 키 상태를 지우고 MOVE(0,0)을 별도로 보내지 않으므로,
이미 진행 중인 수동 이동은 HOME 거절 전에 STOP으로 정리한다.
AUTO도 아직 상태를 변경하거나 Track ID를 선택하지 않는다.

STATUS의 mode는 MANUAL, targetState는 NONE, selected ID는 0이다.
gimbalState는 IDLE 또는 MANUAL_MOVING이고, 장치 I/O 실패 시 ERROR/errorCode=1
(이 구현의 Serial I/O 오류)로 전환한다. 후속 피드백 구현에서 오류 코드를 확장할 수 있다.
이 상태는 보드 애플리케이션의 제어 상태이며 실제 모터 위치 측정값이 아니다.

## 정지와 장애

- TCP 연결 종료, 잘못된 명령으로 인한 세션 폐기, Runtime 종료 시 이동 중이면 T=135를 제출한다.
- 마지막 유효 PING 이후 heartbeat 만료 시 이동 중이면 T=135를 제출하고 IDLE STATUS를 보낸다.
- 연결 직후에는 heartbeat 한 주기만큼 초기 유예를 둔다. MOVE가 PING 시각을 연장하지 않는다.
- heartbeat가 만료된 뒤 유효 PING 없이 들어온 새 이동 요청은 실행하지 않는다.
- 부분 TCP header/body를 기다리는 중에도 짧은 poll 주기로 tick을 처리한다.
- Ctrl+C/SIGTERM은 영상 queue 정리가 끝나기 전에 TCP worker 종료를 요청해 수동 이동을 정리한다.
- 재접속 시 현재 STATUS를 보내며 이전 방향 명령을 저장·재실행하지 않는다.
- Serial write 오류/USB 해제는 STOP을 재시도하고 ERROR STATUS를 보낸다. 실제로 전달하지 못한
  STOP을 성공으로 보고하지 않는다. PING은 계속 응답하지만 이동 요청은 실행하지 않는다.
- 쓰기는 `write_timeout_ms` 안에서 호스트 제출을 끝내도록 제한한다. 무제한 `tcdrain`은 사용하지
  않는다. 호스트 write 성공은 장치 수락·물리 정지 완료의 증명이 아니다.

## 시험 절차와 범위

1. 보드에서 `ls -l /dev/serial/by-id/`로 CP2102N 고정 경로와 접근 권한을 확인한다.
2. 기존 보드 시험 YAML을 복사해 `control.enabled`, `gui.command.handler`, 장치 경로를 설정한다.
3. 새 코드를 보드에 반영하고 `bash scripts/build_runtime.sh`로 다시 빌드한다.
4. PC 앱을 실행하고 새 YAML로 Runtime을 실행한다. 시작 시 STOP만 제출하며 HOME은 하지 않는다.
5. 영상 조작 영역에 포커스를 둔 뒤 방향키 누름·대각·한 축 해제·전체 해제를 확인한다.
   Track ID 텍스트 편집 중에는 PC가 방향키를 제어로 보내지 않는다.
6. PC 종료·연결 해제와 Runtime Ctrl+C에서 수동 이동이 정리되는지 확인한다.

CPU 테스트는 가상 시리얼(PTY)과 실제 loopback TCP로 MOVE→JSON·STATUS, 축 부호·속도 설정,
키 해제, 부분 TCP body 중 heartbeat 정지, 단절·재접속·종료를 확인한다. 가상 장치 전략으로
제어 상태와 Serial 실패 응답을 검증한다. 실제 짐벌 이동·축 방향·정지 지연·장치 피드백은 별도
실기 시험 대상이다. 카메라·DXNN·NPU 추론 검증을 대체하지 않는다.
