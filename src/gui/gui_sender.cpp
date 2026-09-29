#include "gui/gui_sender.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include <pthread.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <opencv2/imgproc.hpp>

#include "common/logger.hpp"

namespace {
// gst_init()은 process 전체에서 한 번만 호출하면 된다. Application에 전역
// 초기화 책임을 추가하지 않도록 첫 GuiSender가 생성될 때 안전하게 초기화한다.
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

// pipeline bus에 올라온 오류를 로그로 옮긴다. GStreamer가 소유한 문자열과
// message는 이 함수 안에서 모두 해제한다.
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

// GStreamer object는 C API 자원이므로 GuiSender의 PImpl 안에 모아 둔다.
// pipeline이 bin에 추가된 하위 element들을 함께 소유하며, stop()에서 pipeline
// 하나를 unref하면 appsrc를 포함한 전체 element가 정리된다.
class GuiSender::Impl
{
private:
    GstElement* pipeline_ = nullptr;
    GstAppSrc* appsrc_ = nullptr; // pipeline 소유 object를 가리키는 borrowed pointer
    int width_ = 0;
    int height_ = 0;
    std::uint64_t frame_index_ = 0;

public:
    ~Impl()
    {
        stop();
    }

    bool start(const GuiVideoConfig& config, int width, int height)
    {
        if (pipeline_)
            return width == width_ && height == height_;

        if (!initialize_gstreamer()) return false;

        GstElement* pipeline = gst_pipeline_new("gui-video-pipeline");
        GstElement* appsrc = gst_element_factory_make("appsrc", "gui-source");
        GstElement* queue = gst_element_factory_make("queue", "latest-frame-queue");
        GstElement* convert = gst_element_factory_make("videoconvert", "gui-convert");
        GstElement* encoder = gst_element_factory_make(
            config.encoder.c_str(), "gui-h264-encoder");
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

        GstCaps* caps = gst_caps_new_simple(
            "video/x-raw",
            "format", G_TYPE_STRING, "BGR",
            "width", G_TYPE_INT, width,
            "height", G_TYPE_INT, height,
            "framerate", GST_TYPE_FRACTION, config.fps, 1,
            nullptr);
        g_object_set(
            appsrc,
            "caps", caps,
            "is-live", TRUE,
            "format", GST_FORMAT_TIME,
            "block", TRUE,
            nullptr);
        gst_caps_unref(caps);

        // encoder가 잠시 느려져도 오래된 화면이 줄지어 쌓이지 않게 한다.
        // queue의 leaky=downstream은 가장 오래된 buffer부터 버린다.
        g_object_set(
            queue,
            "max-size-buffers", 1U,
            "max-size-bytes", 0U,
            "max-size-time", static_cast<guint64>(0),
            "leaky", 2,
            nullptr);

        // x264enc 저지연 설정. Orange Pi 하드웨어 encoder는 element와 속성이
        // 보드 image마다 달라 실제 장치에서 별도 검증한 뒤 추가해야 한다.
        gst_util_set_object_arg(G_OBJECT(encoder), "tune", "zerolatency");
        gst_util_set_object_arg(G_OBJECT(encoder), "speed-preset", "ultrafast");
        g_object_set(
            encoder,
            "bitrate", static_cast<guint>(config.bitrate_kbps),
            "key-int-max", config.fps,
            "bframes", 0,
            "byte-stream", TRUE,
            nullptr);

        g_object_set(parser, "config-interval", -1, nullptr);
        g_object_set(
            payloader,
            "config-interval", 1,
            "pt", 96U,
            "mtu", static_cast<guint>(config.rtp_mtu),
            nullptr);
        g_object_set(
            sink,
            "host", config.host.c_str(),
            "port", static_cast<gint>(config.port),
            "sync", FALSE,
            "async", FALSE,
            nullptr);

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
        width_ = width;
        height_ = height;
        frame_index_ = 0;
        Logger::info(
            "[GuiSender] RTP/UDP 영상 송신 시작: " + config.host + ":" +
            std::to_string(config.port));
        return true;
    }

    bool push(const cv::Mat& image, int fps)
    {
        if (!pipeline_ || !appsrc_ || image.empty() || !image.isContinuous())
            return false;

        const std::size_t byte_size = image.total() * image.elemSize();
        GstBuffer* buffer = gst_buffer_new_allocate(nullptr, byte_size, nullptr);
        if (!buffer) return false;
        if (gst_buffer_fill(buffer, 0, image.data, byte_size) != byte_size)
        {
            gst_buffer_unref(buffer);
            return false;
        }

        const GstClockTime duration = gst_util_uint64_scale_int(
            1, GST_SECOND, fps);
        GST_BUFFER_PTS(buffer) = frame_index_ * duration;
        GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
        GST_BUFFER_DURATION(buffer) = duration;
        ++frame_index_;

        // gst_app_src_push_buffer()가 성공 여부와 관계없이 buffer 소유권을
        // 가져가므로 호출 뒤에는 직접 unref하지 않는다.
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

    void stop()
    {
        if (!pipeline_) return;
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        appsrc_ = nullptr;
        width_ = 0;
        height_ = 0;
        frame_index_ = 0;
    }
};

GuiSender::GuiSender(
    GuiVideoConfig config,
    std::uint16_t clip_min,
    std::uint16_t clip_max)
    : config_(std::move(config)),
      clip_min_(clip_min),
      clip_max_(clip_max),
      impl_(std::make_unique<Impl>())
{
}

GuiSender::~GuiSender() = default;

bool GuiSender::send(const TrackingResult& result)
{
    if (!result.frame || result.frame->image.empty() ||
        result.frame->image.type() != CV_16UC1 || clip_min_ >= clip_max_ ||
        config_.codec != GuiVideoCodec::H264 || config_.host.empty() ||
        config_.port == 0 || config_.fps <= 0)
        return false;

    const cv::Mat& source = result.frame->image;
    if (!impl_->start(config_, source.cols, source.rows))
        return false;

    // OpenCV convertTo는 clip 범위 밖의 값을 0/255로 saturate한다. 이 영상은
    // GUI 표시 전용이며 Detection 입력과 원본 CV_16UC1은 수정하지 않는다.
    const double scale = 255.0 / static_cast<double>(clip_max_ - clip_min_);
    cv::Mat gray8;
    source.convertTo(
        gray8, CV_8UC1, scale,
        -static_cast<double>(clip_min_) * scale);

    cv::Mat display;
    cv::cvtColor(gray8, display, cv::COLOR_GRAY2BGR);

    // Track 좌표는 원본 frame pixel 좌표 계약을 따르므로 resize 없이 그린다.
    for (const Track& track : result.tracks)
    {
        if (track.width <= 0.0F || track.height <= 0.0F) continue;
        const int x1 = std::clamp(
            static_cast<int>(std::lround(track.x)), 0, display.cols - 1);
        const int y1 = std::clamp(
            static_cast<int>(std::lround(track.y)), 0, display.rows - 1);
        const int x2 = std::clamp(
            static_cast<int>(std::lround(track.x + track.width)),
            0, display.cols - 1);
        const int y2 = std::clamp(
            static_cast<int>(std::lround(track.y + track.height)),
            0, display.rows - 1);
        if (x2 <= x1 || y2 <= y1) continue;

        cv::rectangle(
            display, cv::Point(x1, y1), cv::Point(x2, y2),
            cv::Scalar(0, 255, 0), 2);
        cv::putText(
            display, "ID " + std::to_string(track.track_id),
            cv::Point(x1, std::max(14, y1 - 4)), cv::FONT_HERSHEY_SIMPLEX,
            0.5, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
    }

    return impl_->push(display, config_.fps);
}

void GuiSender::stop()
{
    impl_->stop();
}
