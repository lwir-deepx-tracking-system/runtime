#include "pipeline/postprocess_thread.hpp"

#include <utility>
#include <chrono>

#include "common/logger.hpp"


// Postprocessor와 입력/출력 Queue를 Thread에 연결한다.
PostprocessThread::PostprocessThread(
    Postprocessor& postprocessor,
    ThreadSafeQueue<ModelOutput>& input_queue,
    ThreadSafeQueue<DetectionResult>& output_queue,
    bool measurement_enabled)
    : postprocessor_(postprocessor),
      input_queue_(input_queue),
      output_queue_(output_queue),
      measurement_enabled_(measurement_enabled)
{
}


// Postprocess pthread를 생성하고 실행을 시작한다.
void PostprocessThread::start()
{
    pthread_create(
        &thread_,
        nullptr,
        &PostprocessThread::thread_func,
        this
    );
}


// Postprocess Thread가 종료될 때까지 기다린다.
void PostprocessThread::join()
{
    pthread_join(thread_, nullptr);
}


// pthread_create()에서 호출되는 정적 진입 함수.
void* PostprocessThread::thread_func(void* arg)
{
    auto* thread =
        static_cast<PostprocessThread*>(arg);

    thread->run();

    return nullptr;
}


// ModelOutput을 받아 후처리하고 Detection 목록을 전달한다.
void PostprocessThread::run()
{
    Logger::info("[PostprocessThread] 시작");

    ModelOutput output;

    while (input_queue_.pop(output))
    {
        const auto started_at = measurement_enabled_ ?
            std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        DetectionResult result;
        result.detections = postprocessor_.postprocess(output);
        result.metadata = output.metadata;
        if (measurement_enabled_)
        {
            record_stage_metric(metrics_, output.metadata, started_at,
                std::chrono::steady_clock::now());
            result.metadata.enqueued_at = std::chrono::steady_clock::now();
        }

        if (!output_queue_.push(
                std::move(result)))
        {
            break;
        }
    }

    // 더 이상 Detection이 생성되지 않음을 다음 Stage에 알린다.
    output_queue_.close();

    Logger::info("[PostprocessThread] 종료");
}
