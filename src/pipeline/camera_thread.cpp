#include "pipeline/camera_thread.hpp"

#include <utility>
#include <chrono>

#include "common/logger.hpp"


CameraThread::CameraThread(
    Camera& camera,
    ThreadSafeQueue<Frame>& output_queue,
    bool measurement_enabled)
    : camera_(camera),
      output_queue_(output_queue),
      measurement_enabled_(measurement_enabled)
{
}


// Camera pthread를 시작한다.
void CameraThread::start()
{
    pthread_create(
        &thread_,
        nullptr,
        &CameraThread::thread_func,
        this
    );
}


// Camera pthread가 끝날 때까지 기다린다.
void CameraThread::join()
{
    pthread_join(thread_, nullptr);
}


// pthread에서 CameraThread::run()을 실행한다.
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

    if (!camera_.open())
    {
        Logger::error("[CameraThread] 카메라를 열지 못했습니다");
        output_queue_.close();
        return;
    }

    std::uint64_t next_frame_id = 1;

    while (true)
    {
        Frame frame;
        const auto started_at = measurement_enabled_ ?
            std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        if (!camera_.read(frame))
            break;
        const auto finished_at = std::chrono::steady_clock::now();

        // Camera에서 부여한 정보를 이후 모든 stage가 그대로 전달한다.
        frame.metadata.frame_id = next_frame_id++;
        frame.metadata.captured_at = finished_at;
        if (measurement_enabled_)
        {
            record_stage_metric(metrics_, frame.metadata, started_at, finished_at);
            frame.metadata.enqueued_at = std::chrono::steady_clock::now();
        }

        if (!output_queue_.push(std::move(frame)))
            break;
    }

    camera_.close();

    // DetectionThread에 더 이상 Frame이 오지 않음을 알린다.
    output_queue_.close();

    Logger::info("[CameraThread] 종료");
}
