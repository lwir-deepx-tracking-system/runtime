#pragma once

#include <cstdint>
#include <memory>

#include "common/track.hpp"
#include "gui/gui_config.hpp"

// TrackingResult를 GUI 표시용 RTP/UDP 영상으로 보내는 경계 클래스.
//
// appsrc -> videoconvert -> H.264 encoder -> rtph264pay -> udpsink 파이프라인을
// 이 클래스가 소유한다. 모델 입력용 letterbox 영상이 아니라 원본 크기의
// LWIR 프레임을 사용하므로 Track 좌표와 표시 위치가 일치한다.
class GuiSender
{
private:
    // GStreamer 타입을 public header에 노출하지 않도록 구현 세부사항을 감춘다.
    // Impl이 GstElement pipeline의 생성부터 해제까지 책임진다.
    class Impl;

    GuiVideoConfig config_;
    std::uint16_t clip_min_;
    std::uint16_t clip_max_;
    std::unique_ptr<Impl> impl_;

public:
    GuiSender(
        GuiVideoConfig config,
        std::uint16_t clip_min,
        std::uint16_t clip_max);
    ~GuiSender();

    GuiSender(const GuiSender&) = delete;
    GuiSender& operator=(const GuiSender&) = delete;

    // 다음 순서로 표시 영상을 만든 뒤 네트워크로 전송한다.
    // 1. CV_16UC1과 clip 범위를 검증한다.
    // 2. GUI 표시용 CV_8UC1 영상으로 변환한다.
    // 3. Track box와 ID를 원본 좌표로 그린다.
    // 4. GStreamer appsrc에 밀어 넣어 H.264/RTP/UDP로 전송한다.
    bool send(const TrackingResult& result);

    // pipeline을 NULL state로 내리고 GStreamer 자원을 해제한다.
    void stop();
};
