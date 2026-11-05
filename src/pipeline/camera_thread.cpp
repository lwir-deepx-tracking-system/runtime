#include "pipeline/camera_thread.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

#include "common/logger.hpp"


CameraThread::CameraThread(
    Camera& camera,
    ThreadSafeQueue<FrameMessage>& output_queue,
    bool measurement_enabled, bool retain_metrics)
    : camera_(camera),
      output_queue_(output_queue),
      measurement_enabled_(measurement_enabled), retain_metrics_(retain_metrics)
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

// thread에서 CameraThread::run()을 실행한다.
void* CameraThread::thread_func(void* arg)
{
    auto* thread =
        static_cast<CameraThread*>(arg);

    thread->run();

    return nullptr;
}

// Camera Frame을 읽어 다음 Stage Queue로 전달한다.
void CameraThread::run()
{
    Logger::info("[CameraThread] 시작");

    try
    {
        if (!camera_.open([this] { return stop_requested_.load(); }) && !stop_requested_.load())
            throw std::runtime_error("카메라를 열지 못했습니다");
        
        std::uint64_t next_frame_id = 1;
        while (!stop_requested_.load())
        {
            auto frame = std::make_shared<FrameContext>();
            const auto started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};

            if (!camera_.capture(*frame))
                continue;

            const auto finished_at = std::chrono::steady_clock::now();
            if (stop_requested_.load()) break;

            // Camera에서 부여한 정보를 이후 모든 stage가 그대로 전달한다.
            frame->metadata.frame_id = next_frame_id++;
            frame->metadata.captured_at = finished_at;
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
            if (!output_queue_.push_latest(std::move(message), 2))
                break;
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
