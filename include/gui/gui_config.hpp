#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// GUI에 표시할 영상은 Orange Pi에서 H.264로 인코딩한 뒤 RTP/UDP로 보낸다.
// 현재는 H.264 한 종류만 지원하지만, 문자열 대신 enum을 사용해 잘못된
// codec 값이 송신기까지 흘러가지 않도록 한다.
enum class GuiVideoCodec
{
    H264
};

struct GuiVideoConfig
{
    std::string host;       // 영상을 받을 GUI 장치의 주소
    std::uint16_t port;     // RTP/UDP 목적지 포트
    GuiVideoCodec codec;
    std::string encoder;    // 현재 검증된 GStreamer encoder element
    int bitrate_kbps;       // H.264 목표 비트레이트
    int fps;                // appsrc에 전달할 영상 프레임률
    int rtp_mtu;            // IP 단편화를 피하기 위한 RTP packet 크기
};

// Track 정보는 영상 픽셀에 그리지 않고 별도 UDP datagram으로 전송한다.
// 매 frame의 frame_id, RTP timestamp와 원본 좌표 bbox가 들어가므로 GUI가
// 영상 frame에 맞춰 박스를 그리고 선택한 track_id를 TCP로 돌려보낼 수 있다.
struct GuiMetadataConfig
{
    bool enabled;
    std::string host;
    std::uint16_t port;
    std::size_t max_packet_bytes;
};

// GUI에서 선택한 track_id 같은 작은 제어 명령은 손실되면 안 되므로 TCP로
// 받는다. 영상 UDP 포트와 섞지 않고 독립된 포트를 사용한다.
struct GuiCommandConfig
{
    bool enabled;
    std::string bind_address;   // Orange Pi가 명령을 기다릴 주소
    std::uint16_t port;         // TCP listen 포트
    int receive_timeout_ms;     // 종료 요청을 주기적으로 확인할 간격
};

// AppConfig가 runtime.yaml의 gui 항목을 파싱하고 검증한 뒤 이 구조체를
// GUI 컴포넌트에 전달한다. GuiSender와 GuiReceiver는 YAML을 직접 읽지 않는다.
struct GuiConfig
{
    bool enabled;
    GuiVideoConfig video;
    GuiMetadataConfig metadata;
    GuiCommandConfig command;
};
