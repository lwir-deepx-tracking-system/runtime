#pragma once

#include <cstdint>

#include <sys/socket.h>

#include "common/track.hpp"
#include "gui/gui_config.hpp"

struct _GstAppSrc;
struct _GstElement;

// TrackingResult를 GUI 표시용 RTP/UDP 영상으로 보내는 경계 클래스.
//
// 영상은 bbox를 그리지 않은 채 H.264/RTP/UDP로 보내고, Track 목록은 원본
// pixel 좌표 metadata UDP datagram으로 별도 전송한다.
class GuiSender
{
private:
    GuiVideoConfig video_config_;
    GuiMetadataConfig metadata_config_;
    std::uint16_t clip_min_;
    std::uint16_t clip_max_;

    // GuiSender가 GStreamer pipeline 수명을 직접 소유한다. appsrc_는 pipeline
    // 내부 element를 가리키며 pipeline_을 해제할 때 함께 정리된다.
    _GstElement* pipeline_ = nullptr;
    _GstAppSrc* appsrc_ = nullptr;
    int frame_width_ = 0;
    int frame_height_ = 0;
    std::uint64_t frame_index_ = 0;

    // bbox metadata용 POSIX UDP socket과 목적지 주소도 GuiSender가 소유한다.
    int metadata_fd_ = -1;
    sockaddr_storage metadata_address_{};
    socklen_t metadata_address_size_ = 0;

    bool start_gstreamer_pipeline(int width, int height);
    bool push_frame_to_gstreamer(const cv::Mat& image);
    bool open_metadata_socket();
    bool send_tracking_metadata(const TrackingResult& result);

public:
    GuiSender(
        GuiVideoConfig video_config,
        GuiMetadataConfig metadata_config,
        std::uint16_t clip_min,
        std::uint16_t clip_max);
    ~GuiSender();

    GuiSender(const GuiSender&) = delete;
    GuiSender& operator=(const GuiSender&) = delete;

    // 다음 순서로 영상과 metadata를 각각 전송한다.
    // 1. CV_16UC1과 clip 범위를 검증한다.
    // 2. GUI 표시용 CV_8UC1 영상으로 변환한다.
    // 3. 박스 없는 영상을 GStreamer로 H.264/RTP/UDP 전송한다.
    // 4. frame_id와 Track bbox를 별도 UDP port로 전송한다.
    bool send(const TrackingResult& result);

    // pipeline을 NULL state로 내리고 GStreamer 자원을 해제한다.
    void stop();
};
