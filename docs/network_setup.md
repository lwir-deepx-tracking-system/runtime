# PC–Orange Pi 직결 네트워크

영상·메타데이터·제어 통신은 고정 IPv4를 사용한다. 네트워크 설정은 OS에 한 번
저장하며 매 Runtime 실행마다 주소를 바꾸지 않는다.

| 장비 / 용도 | 주소 | 물리 포트 식별 |
|---|---|---|
| Windows PC 직결 Ethernet | 192.168.50.1/24 | MAC 8C:B0:E9:1C:F9:F9 |
| Orange Pi PC 직결 Ethernet | 192.168.50.2/24 | MAC C0:74:2B:FD:88:71, 현재 enP4p65s0 |
| Orange Pi 기존 인터넷 연결 | 192.168.137.10/24 | MAC C0:74:2B:FD:88:70, 현재 enP3p49s0 |

위 MAC은 현재 장비의 식별값이며 다른 장비에서는 확인 후 바꾼다. 두 보드 포트에
같은 192.168.50.2 주소를 동시에 설정하지 않는다. PC 직결 케이블은 표의 전용 포트에
계속 연결한다. 포트에 PC 직결 라벨을 붙이면 혼동을 줄일 수 있다.

## Orange Pi의 영구 연결 프로필

NetworkManager 프로필 `lwir-pc-link`를 실제 보드에 생성했다.

- 수동 IPv4: 192.168.50.2/24
- 자동 연결: yes, 우선순위 100
- 인터페이스 이름 제한 없음, MAC C0:74:2B:FD:88:71에 매칭
- Gateway/DNS 지정 없음, 기본 경로로 사용하지 않음
- 이 전용 프로필에서 IPv6 비활성화

MAC 매칭과 자동 연결은 [NetworkManager 연결 설정](https://networkmanager.dev/docs/api/latest/nm-settings-nmcli.html)을 따른다.
인터페이스 이름이 바뀌어도 같은 MAC의 장치에 적용된다. 다른 물리 포트로 옮기는
기능이나 상대 PC 자동 탐색을 구현한 것은 아니다. 보드의 기존 인터넷 프로필은 유지한다.

아래 명령은 프로필이 없는 새 환경에서 생성할 때 사용한다. 장비의 실제 MAC을 먼저 확인한다.

```bash
nmcli connection add type ethernet con-name lwir-pc-link ifname '*' \
  802-3-ethernet.mac-address C0:74:2B:FD:88:71 \
  ipv4.method manual ipv4.addresses 192.168.50.2/24 \
  ipv4.never-default yes ipv6.method disabled \
  connection.autoconnect yes connection.autoconnect-priority 100
nmcli connection up lwir-pc-link
```

현재 보드에서는 이미 생성·활성화했으므로 add를 반복하지 않는다. 확인 명령:

```bash
nmcli connection show lwir-pc-link
ip -br -4 address
ip -4 route get 192.168.50.1
```

자동 연결 설정은 저장했지만 실제 재부팅·케이블 탈착 시험은 수행하지 않았다.

## Application 설정

Orange Pi `config/runtime.yaml`:

- `gui.video.host`: 192.168.50.1, UDP 5000
- `gui.metadata.host`: 192.168.50.1, UDP 5001
- `gui.command.bind_address`: 192.168.50.2 또는 0.0.0.0, TCP 5002

PC `config/pc-application.yaml`:

- `video.bind_address`, `metadata.bind_address`: 0.0.0.0 또는 192.168.50.1
- `control.host`: 192.168.50.2, TCP 5002

PC의 이더넷 어댑터는 이미 192.168.50.1/24로 설정되어 있다. 직결 링크에는 기본
Gateway를 지정하지 않는다. PC 인터넷 연결은 Wi-Fi 등 기존 연결을 사용한다.
앱의 TCP/UDP 포트 번호는 고정하며 물리 LAN 포트·인터페이스 이름과 구분한다.

## Windows 수신 규칙과 확인 범위

현재 `build/Debug/pc-application.exe`의 Public 프로필 TCP·UDP 수신 규칙은
활성화된 Allow로 확인했다. 이 작업에서 Windows 방화벽 규칙은 변경하지 않았다.
다른 실행 파일 경로로 빌드·실행하면 해당 경로의 규칙을 따로 확인해야 한다.
방화벽 프로필·조직 정책 등의 영향까지 실제 수신 성공으로 검증한 것은 아니다.

PC → 보드 192.168.50.2의 TCP 22 연결은 확인했다. 보드 → PC ping 무응답만으로
직결 연결 실패를 판정하지 않는다. UDP 영상·메타데이터 수신은 실제 PC Application으로
확인해야 한다. 카메라·NPU·짐벌 검증을 대신하지 않는다.
