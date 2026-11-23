#include "pipeline/camera_thread.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

#include "common/logger.hpp"
#include "common/pipeline_performance.hpp"
#include "app/raw_recorder.hpp"


CameraThread::CameraThread(
    Camera& camera,
    ThreadSafeQueue<FrameMessage>& output_queue,
    bool measurement_enabled, bool retain_metrics, RawRecorder* recorder, PipelinePerformance* performance,
    CameraRecoveryConfig recovery)
    : camera_(camera),
      output_queue_(output_queue),
      measurement_enabled_(measurement_enabled), retain_metrics_(retain_metrics), recorder_(recorder), performance_(performance), recovery_(recovery)
{
}


// Camera thread를 시작한다.
void CameraThread::start()
{
    if (started_) return;
    thread_ = std::thread(&CameraThread::thread_func, this);
    started_ = true;
}


// Camera thread가 끝날 때까지 기다린다.
void CameraThread::join()
{
    if (!started_) return;
    if(thread_.joinable()) thread_.join();
    started_ = false;
}

void CameraThread::request_stop()
{
    stop_requested_.store(true);
}

// GUI requests are queued; the capture thread alone may reopen the SDK handle.
bool CameraThread::request_reopen()
{
    return recovery_.attempts && !stop_requested_.load() && !finished_.load() &&
        !reopen_requested_.exchange(true);
}

// thread에서 CameraThread::run()을 실행한다.
void* CameraThread::thread_func(void* arg)
{
    auto* thread =
        static_cast<CameraThread*>(arg);

    thread->run();

    return nullptr;
}

// The SDK device is opened, retried and closed only by the capture thread.
bool CameraThread::open_camera()
{
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(recovery_.stall_ms);
    return camera_.open([&] {
        return stop_requested_.load() ||
            (recovery_.attempts && std::chrono::steady_clock::now() >= deadline);
    });
}

bool CameraThread::reopen_camera(const char* reason, unsigned& attempts)
{
    if (stop_requested_.load()) return false;
    Logger::warn(std::string("[CameraThread] ") + reason);
    camera_.close();
    while (!stop_requested_.load() && attempts < recovery_.attempts) {
        ++attempts;
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(recovery_.retry_ms);
        while (!stop_requested_.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!stop_requested_.load() && open_camera()) {
            Logger::info("[CameraThread] 카메라 다시 열기 성공");
            have_scene_ = false;
            identical_ = 0;
            return true;
        }
    }
    if (stop_requested_.load()) return false;
    throw std::runtime_error(std::string(reason) + ": camera recovery exhausted");
}

// Detect repeated raw samples before publishing a potentially frozen stream.
bool CameraThread::repeated_scene(const cv::Mat& image)
{
    if (!recovery_.attempts || !recovery_.identical_frames || image.empty() || image.type() != CV_16UC1)
        return false;
    std::array<std::uint16_t, 64> scene;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            scene[y * 8 + x] = image.at<std::uint16_t>((2*y + 1)*image.rows/16,
                                                      (2*x + 1)*image.cols/16);
    identical_ = have_scene_ && scene == previous_scene_ ? identical_ + 1 : 0;
    previous_scene_ = scene;
    have_scene_ = true;
    return identical_ >= recovery_.identical_frames;
}

// Camera Frame을 읽어 다음 Stage Queue로 전달한다.
void CameraThread::run()
{
    Logger::info("[CameraThread] 시작");

    try
    {
        using Clock = std::chrono::steady_clock;
        unsigned attempts = 0;
        unsigned stable_frames = 0;
        if (!open_camera() && !stop_requested_.load())
            reopen_camera("카메라를 열지 못했습니다", attempts);
        auto last_frame = Clock::now();
        std::uint64_t next_frame_id = 1;
        while (!stop_requested_.load())
        {
            if (reopen_requested_.exchange(false)) {
                attempts = 0;
                if (!reopen_camera("카메라 다시 열기 요청", attempts)) break;
                last_frame = Clock::now();
                stable_frames = 0;
            }
            auto frame = std::make_shared<FrameContext>();
            const auto started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};

            const bool captured = camera_.capture(*frame);
            const auto finished_at = Clock::now();
            if (stop_requested_.load()) break;
            const bool frozen = captured && repeated_scene(frame->image);
            if (!captured || frozen) {
                if (recovery_.attempts && (frozen ||
                    finished_at - last_frame >= std::chrono::milliseconds(recovery_.stall_ms))) {
                    if (!reopen_camera(frozen ? "반복 프레임 감지" : "카메라 프레임 수신 시간 초과",
                                       attempts)) break;
                    last_frame = Clock::now();
                    stable_frames = 0;
                } else if (!captured) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                continue;
            }
            last_frame = finished_at;
            if (attempts && ++stable_frames >= 30) attempts = 0;

            // Camera에서 부여한 정보를 이후 모든 stage가 그대로 전달한다.
            frame->metadata.frame_id = next_frame_id++;
            frame->metadata.captured_at = finished_at;
            if (recorder_) recorder_->submit(frame);
            if (measurement_enabled_)
                frame->metadata.capture_processing_ms =
                    std::chrono::duration<double, std::milli>(finished_at - started_at).count();
            FrameMessage message;
            message.frame = std::move(frame);
            if (measurement_enabled_)
            {
                if (retain_metrics_) record_stage_metric(
                    metrics_, message.frame->metadata, {}, started_at,
                    started_at, finished_at);
                message.enqueued_at = std::chrono::steady_clock::now();
            }

            // Keep only the latest two frames while detection is behind.
            std::size_t dropped = 0;
            if (!output_queue_.push_latest(std::move(message), 2, performance_ ? &dropped : nullptr))
                break;
            if (performance_) performance_->capture(dropped);
        }
    }
    catch (const std::exception& e)
    {
        error_message_ = e.what();
        failed_.store(true);
        Logger::error("[CameraThread] " + error_message_);
    }
    catch (...)
    {
        error_message_ = "알 수 없는 치명적 오류";
        failed_.store(true);
        Logger::error("[CameraThread] " + error_message_);
    }

    camera_.close();
    output_queue_.close();
    finished_.store(true);
    Logger::info("[CameraThread] 종료");
}
