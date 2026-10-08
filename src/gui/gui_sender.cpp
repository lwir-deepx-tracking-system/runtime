#include "gui/gui_sender.hpp"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <opencv2/imgproc.hpp>

#include "common/logger.hpp"
#include "gui/gui_protocol.hpp"

namespace {
// 여러 GuiSender 인스턴스가 생겨도 process 전체의 GStreamer 초기화는 한 번만 한다.
pthread_once_t g_gstreamer_once = PTHREAD_ONCE_INIT;

void initialize_gstreamer_once()
{
    gst_init(nullptr, nullptr);
}

bool initialize_gstreamer()
{
    const int result = pthread_once(
        &g_gstreamer_once, &initialize_gstreamer_once);
    if (result != 0)
        Logger::error("[GuiSender] pthread_once GStreamer 초기화 실패");
    return result == 0;
}

// 비동기 pipeline에서 발생한 오류를 bus에서 회수해 호출자에게 실패로 알린다.
// appsrc push가 성공해도 encoder나 sink에서 뒤늦게 실패할 수 있기 때문이다.
bool log_pipeline_error(GstElement* pipeline)
{
    GstBus* bus = gst_element_get_bus(pipeline);
    GstMessage* message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
    gst_object_unref(bus);
    if (!message) return false;

    GError* error = nullptr;
    gchar* debug = nullptr;
    gst_message_parse_error(message, &error, &debug);
    Logger::error(
        std::string("[GuiSender] GStreamer 오류: ") +
        (error ? error->message : "unknown error"));
    if (debug)
        Logger::debug(std::string("[GuiSender] GStreamer debug: ") + debug);
    if (error) g_error_free(error);
    g_free(debug);
    gst_message_unref(message);
    return true;
}
}

GuiSender::GuiSender(
    GuiVideoConfig video_config,
    GuiMetadataConfig metadata_config,
    std::uint16_t clip_min,
    std::uint16_t clip_max)
    : video_config_(std::move(video_config)),
      metadata_config_(std::move(metadata_config)),
      clip_min_(clip_min),
      clip_max_(clip_max)
{
}

GuiSender::~GuiSender()
{
    stop();
}

// 첫 유효 frame의 크기와 설정값으로 H.264/RTP 송신 pipeline을 준비한다.
// 성공하면 이후 frame은 같은 appsrc와 pipeline을 계속 재사용한다.
bool GuiSender::start_gstreamer_pipeline(int width, int height)
{
    // 실행 중 frame 크기가 바뀌면 기존 caps와 맞지 않으므로 재사용하지 않는다.
    if (pipeline_)
        return width == frame_width_ && height == frame_height_;
    if (!initialize_gstreamer()) return false;

    // GStreamer 영상 경로:
    //
    // OpenCV BGR frame
    //   -> appsrc       : C++ frame을 GStreamer에 입력
    //   -> queue        : frame 생산과 encoder 처리 속도를 분리
    //   -> videoconvert : encoder가 받을 수 있는 pixel format으로 변환
    //   -> x264enc      : H.264 압축
    //   -> h264parse    : H.264 bitstream과 parameter set 정리
    //   -> rtph264pay   : H.264 데이터를 RTP packet으로 분할
    //   -> udpsink      : PC GUI의 video UDP 포트로 송신
    GstElement* pipeline = gst_pipeline_new("gui-video-pipeline");
    GstElement* appsrc = gst_element_factory_make("appsrc", "gui-source");
    GstElement* queue = gst_element_factory_make("queue", "latest-frame-queue");
    GstElement* convert = gst_element_factory_make("videoconvert", "gui-convert");
    GstElement* encoder = gst_element_factory_make(
        video_config_.encoder.c_str(), "gui-h264-encoder");
    GstElement* parser = gst_element_factory_make("h264parse", "gui-h264-parser");
    GstElement* payloader = gst_element_factory_make("rtph264pay", "gui-rtp-payloader");
    GstElement* sink = gst_element_factory_make("udpsink", "gui-udp-sink");

    if (!pipeline || !appsrc || !queue || !convert || !encoder ||
        !parser || !payloader || !sink)
    {
        Logger::error(
            "[GuiSender] 필요한 GStreamer element를 생성하지 못했습니다. "
            "x264/rtp/udp plugin 설치 상태를 확인하세요");
        if (appsrc) gst_object_unref(appsrc);
        if (queue) gst_object_unref(queue);
        if (convert) gst_object_unref(convert);
        if (encoder) gst_object_unref(encoder);
        if (parser) gst_object_unref(parser);
        if (payloader) gst_object_unref(payloader);
        if (sink) gst_object_unref(sink);
        if (pipeline) gst_object_unref(pipeline);
        return false;
    }

    // appsrc 입력 계약을 BGR, 고정 크기, 설정 FPS로 명시해 downstream이
    // OpenCV buffer 형식을 추측하지 않도록 한다.
    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "BGR",
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, video_config_.fps, 1,
        nullptr);
    g_object_set(
        appsrc,
        "caps", caps,
        "is-live", TRUE,
        "format", GST_FORMAT_TIME,
        "block", TRUE,
        nullptr);
    gst_caps_unref(caps);

    // 실시간 화면은 모든 frame 보존보다 최신 frame 유지가 중요하다.
    // encoder가 잠시 느려지면 이전 frame을 버려 화면 지연 누적을 막는다.
    g_object_set(
        queue,
        "max-size-buffers", 1U,
        "max-size-bytes", 0U,
        "max-size-time", static_cast<guint64>(0),
        "leaky", 2,
        nullptr);

    // GUI 표시 지연을 줄이기 위해 재정렬 지연을 없애고 빠른 encode 설정을 쓴다.
    gst_util_set_object_arg(G_OBJECT(encoder), "tune", "zerolatency");
    gst_util_set_object_arg(G_OBJECT(encoder), "speed-preset", "ultrafast");
    g_object_set(
        encoder,
        "bitrate", static_cast<guint>(video_config_.bitrate_kbps),
        "key-int-max", video_config_.fps,
        "bframes", 0,
        "byte-stream", TRUE,
        nullptr);

    // 수신 GUI가 중간에 연결되어도 decoder 설정을 얻을 수 있도록 parameter set을
    // 반복하고, metadata와 대응할 RTP timestamp 시작 offset은 0으로 고정한다.
    g_object_set(parser, "config-interval", -1, nullptr);
    g_object_set(
        payloader,
        "config-interval", 1,
        "pt", 96U,
        "timestamp-offset", 0U,
        "mtu", static_cast<guint>(video_config_.rtp_mtu),
        nullptr);
    g_object_set(
        sink,
        "host", video_config_.host.c_str(),
        "port", static_cast<gint>(video_config_.port),
        "sync", FALSE,
        "async", FALSE,
        nullptr);

    // element를 하나의 pipeline에 넣고 위에 설명한 순서로 연결한다.
    gst_bin_add_many(
        GST_BIN(pipeline), appsrc, queue, convert, encoder, parser,
        payloader, sink, nullptr);
    if (!gst_element_link_many(
            appsrc, queue, convert, encoder, parser, payloader, sink,
            nullptr))
    {
        Logger::error("[GuiSender] GStreamer element 연결에 실패했습니다");
        gst_object_unref(pipeline);
        return false;
    }

    // PLAYING 이후부터 appsrc에 전달한 frame이 비동기로 encode·송신된다.
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) ==
        GST_STATE_CHANGE_FAILURE)
    {
        Logger::error("[GuiSender] GStreamer pipeline을 시작하지 못했습니다");
        log_pipeline_error(pipeline);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return false;
    }

    pipeline_ = pipeline;
    appsrc_ = GST_APP_SRC(appsrc);
    frame_width_ = width;
    frame_height_ = height;
    frame_index_ = 0;
    Logger::info(
        "[GuiSender] RTP/UDP 영상 송신 시작: " + video_config_.host + ":" +
        std::to_string(video_config_.port));
    return true;
}

// 연속 메모리 BGR image를 timestamp가 있는 GstBuffer로 복사해 appsrc에 넘긴다.
bool GuiSender::push_frame_to_gstreamer(const cv::Mat& image)
{
    if (!pipeline_ || !appsrc_ || image.empty() || !image.isContinuous())
        return false;

    // OpenCV가 소유한 memory와 pipeline의 비동기 수명을 분리하기 위해
    // GstBuffer를 만들고 현재 frame bytes를 복사한다.
    const std::size_t byte_size = image.total() * image.elemSize();
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, byte_size, nullptr);
    if (!buffer) return false;
    if (gst_buffer_fill(buffer, 0, image.data, byte_size) != byte_size)
    {
        gst_buffer_unref(buffer);
        return false;
    }

    // frame_index와 설정 FPS로 연속 PTS를 생성한다. rtph264pay는 이 시간을
    // 90kHz RTP clock으로 변환하고 metadata도 같은 index를 기준으로 계산한다.
    const GstClockTime duration = gst_util_uint64_scale_int(
        1, GST_SECOND, video_config_.fps);
    GST_BUFFER_PTS(buffer) = frame_index_ * duration;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buffer) = duration;
    ++frame_index_;

    // push_buffer 호출 뒤 GstBuffer 소유권은 appsrc로 이동한다.
    const GstFlowReturn result = gst_app_src_push_buffer(appsrc_, buffer);
    if (result != GST_FLOW_OK)
    {
        Logger::error(
            "[GuiSender] appsrc push 실패: " +
            std::to_string(static_cast<int>(result)));
        log_pipeline_error(pipeline_);
        return false;
    }
    return !log_pipeline_error(pipeline_);
}

// 설정된 PC GUI 주소로 metadata를 보낼 UDP socket과 목적지 주소를 준비한다.
bool GuiSender::open_metadata_socket()
{
    if (!metadata_config_.enabled || metadata_fd_ >= 0) return true;

    // metadata는 H.264/RTP pipeline과 독립된 UDP datagram 채널이다.
    // getaddrinfo를 사용해 IPv4/IPv6 어느 주소든 설정값으로 지정할 수 있게 한다.
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* addresses = nullptr;
    const std::string port = std::to_string(metadata_config_.port);
    const int lookup = ::getaddrinfo(
        metadata_config_.host.c_str(), port.c_str(), &hints, &addresses);
    if (lookup != 0)
    {
        Logger::error(
            "[GuiSender] metadata 주소 해석 실패: " +
            std::string(gai_strerror(lookup)));
        return false;
    }

    for (addrinfo* address = addresses; address; address = address->ai_next)
    {
        if (address->ai_addrlen > sizeof(metadata_address_)) continue;
        const int fd = ::socket(
            address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;

        metadata_fd_ = fd;
        std::memcpy(
            &metadata_address_, address->ai_addr, address->ai_addrlen);
        metadata_address_size_ = address->ai_addrlen;
        break;
    }
    ::freeaddrinfo(addresses);

    if (metadata_fd_ < 0)
    {
        Logger::error("[GuiSender] metadata UDP socket 생성 실패");
        return false;
    }
    Logger::info(
        "[GuiSender] bbox metadata UDP 송신 시작: " +
        metadata_config_.host + ":" + port);
    return true;
}

// 현재 TrackingResult를 영상 frame과 대응 가능한 metadata datagram으로 보낸다.
bool GuiSender::send_tracking_metadata(
    const TrackingResult& result,
    std::uint64_t gui_started_us)
{
    if (!metadata_config_.enabled) return true;
    if (!open_metadata_socket()) return false;

    // GUI는 frame_id와 RTP timestamp로 metadata를 영상 frame에 대응시키고,
    // 원본 frame 크기와 bbox를 사용해 영상 위에 Track을 표시한다.
    GuiTrackingMetadata metadata;
    metadata.frame_id = result.frame->metadata.frame_id;
    // metadata와 RTP 영상을 같은 frame으로 대응시키기 위한 timestamp이다.
    // GStreamer H.264 RTP clock의 90kHz 기준으로 현재 frame index를 변환한다.
    metadata.rtp_timestamp = static_cast<std::uint32_t>(
        gst_util_uint64_scale(frame_index_, 90000, video_config_.fps));
    metadata.gui_started_us = gui_started_us;
    metadata.width = static_cast<std::uint32_t>(result.frame->image.cols);
    metadata.height = static_cast<std::uint32_t>(result.frame->image.rows);
    metadata.tracks = result.tracks;

    // C++ Track 구조체를 직접 보내지 않고 protocol의 고정 byte layout으로 만든다.
    std::vector<std::uint8_t> packet;
    if (!encode_tracking_metadata(
            metadata, metadata_config_.max_packet_bytes, packet))
    {
        Logger::error(
            "[GuiSender] Track metadata가 설정한 UDP packet 크기를 초과했습니다");
        return false;
    }

    const ssize_t sent = ::sendto(
        metadata_fd_, packet.data(), packet.size(), 0,
        reinterpret_cast<const sockaddr*>(&metadata_address_),
        metadata_address_size_);
    return sent == static_cast<ssize_t>(packet.size());
}

// 원본 LWIR frame을 표시 영상으로 변환하고 영상/metadata 두 채널에 전달한다.
bool GuiSender::send(
    const TrackingResult& result,
    std::uint64_t gui_started_us)
{
    // 잘못된 frame이나 네트워크 설정은 pipeline 일부를 시작하기 전에 거부한다.
    if (!result.frame || result.frame->image.empty() ||
        result.frame->image.type() != CV_16UC1 || clip_min_ >= clip_max_ ||
        video_config_.codec != GuiVideoCodec::H264 ||
        video_config_.host.empty() || video_config_.port == 0 ||
        video_config_.fps <= 0)
        return false;

    const cv::Mat& source = result.frame->image;
    if (!start_gstreamer_pipeline(source.cols, source.rows)) return false;

    // 16-bit LWIR 원본을 GUI 표시용 8-bit 영상으로 변환한다. 설정한
    // clip_min~clip_max 범위를 0~255로 매핑하며 원본 frame은 수정하지 않는다.
    // bbox는 영상에 굽지 않고 별도 metadata의 원본 좌표로 GUI가 그린다.
    const double scale = 255.0 / static_cast<double>(clip_max_ - clip_min_);
    cv::Mat gray8;
    source.convertTo(
        gray8, CV_8UC1, scale,
        -static_cast<double>(clip_min_) * scale);

    cv::Mat display;
    cv::cvtColor(gray8, display, cv::COLOR_GRAY2BGR);

    // 영상과 Tracking metadata는 서로 다른 UDP 채널로 전송한다.
    // Video: BGR -> H.264 -> RTP -> UDP
    // Metadata: frame_id / RTP timestamp / bbox / track_id -> custom UDP packet
    if (!send_tracking_metadata(result, gui_started_us)) return false;
    return push_frame_to_gstreamer(display);
}

// 재호출해도 안전하게 GStreamer와 metadata socket 소유권을 정리한다.
void GuiSender::stop()
{
    // pipeline을 NULL로 내려 내부 worker와 plugin 자원을 먼저 정리한 뒤
    // metadata socket까지 닫아 GuiSender가 소유한 송신 자원을 모두 해제한다.
    if (pipeline_)
    {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        appsrc_ = nullptr;
        frame_width_ = 0;
        frame_height_ = 0;
        frame_index_ = 0;
    }

    if (metadata_fd_ >= 0)
    {
        ::close(metadata_fd_);
        metadata_fd_ = -1;
        metadata_address_size_ = 0;
    }
}
