#include "pipeline/detection_thread.hpp"

#include <chrono>
#include <exception>
#include <string>
#include <utility>

#include "common/logger.hpp"

// Detection 구현과 shared Frame 입출력 queue를 worker에 연결한다.
DetectionThread::DetectionThread(
    DetectionPipeline& pipeline,
    ThreadSafeQueue<FrameMessage>& input_queue,
    ThreadSafeQueue<DetectionResult>& output_queue,
    bool measurement_enabled)
    : pipeline_(pipeline),
      input_queue_(input_queue),
      output_queue_(output_queue),
      measurement_enabled_(measurement_enabled)
{
}

// pthread가 정적 trampoline을 통해 이 객체의 run()을 실행하게 한다.
void DetectionThread::start()
{
    pthread_create(&thread_, nullptr, &DetectionThread::thread_func, this);
}

// Detection queue 종료까지 worker가 완전히 끝나기를 기다린다.
void DetectionThread::join()
{
    pthread_join(thread_, nullptr);
}

// C 함수 포인터 형태가 필요한 pthread와 C++ 객체 메서드를 연결한다.
void* DetectionThread::thread_func(void* arg)
{
    static_cast<DetectionThread*>(arg)->run();
    return nullptr;
}

void DetectionThread::run()
{
    Logger::info("[DetectionThread] 시작");

    try
    {
        FrameMessage input;
        while (input_queue_.pop(input))
        {
            // 비어 있는 메시지는 downstream으로 전달하지 않는다.
            if (!input.frame)
                continue;

            const auto started_at = measurement_enabled_ ?
                std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point{};

            DetectionResult result;
            // image를 복사하지 않고 Camera가 만든 동일한 FrameContext를 결과에 연결한다.
            result.frame = input.frame;
            result.detections = pipeline_.detect(*input.frame);

            if (measurement_enabled_)
            {
                const auto finished_at = std::chrono::steady_clock::now();
                record_stage_metric(
                    metrics_, input.frame->metadata, input.enqueued_at,
                    started_at, finished_at);
                result.enqueued_at = finished_at;
            }

            if (!output_queue_.push(std::move(result)))
                break;
        }
    }
    catch (const std::exception& e)
    {
        // Detection 실패 시 downstream이 영원히 기다리지 않도록 queue를 닫고 종료한다.
        Logger::error(std::string("[DetectionThread] ") + e.what());
    }

    output_queue_.close();
    Logger::info("[DetectionThread] 종료");
}
