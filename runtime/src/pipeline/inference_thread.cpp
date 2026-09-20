#include "pipeline/inference_thread.hpp"

#include <utility>
#include <chrono>

#include "common/logger.hpp"


InferenceThread::InferenceThread(
    Inference& inference,
    ThreadSafeQueue<ModelInput>& input_queue,
    ThreadSafeQueue<ModelOutput>& output_queue,
    bool measurement_enabled)
    : inference_(inference),
      input_queue_(input_queue),
      output_queue_(output_queue),
      measurement_enabled_(measurement_enabled)
{
}


// Inference pthread를 시작한다.
void InferenceThread::start()
{
    pthread_create(
        &thread_,
        nullptr,
        &InferenceThread::thread_func,
        this
    );
}


// Inference pthread가 종료될 때까지 기다린다.
void InferenceThread::join()
{
    pthread_join(thread_, nullptr);
}


// pthread에서 InferenceThread::run()을 실행한다.
void* InferenceThread::thread_func(void* arg)
{
    auto* thread =
        static_cast<InferenceThread*>(arg);

    thread->run();

    return nullptr;
}


// ModelInput을 받아 추론한 뒤 ModelOutput을 다음 Queue로 전달한다.
void InferenceThread::run()
{
    Logger::info("[InferenceThread] 시작");

    ModelInput input;

    while (input_queue_.pop(input))
    {
        const auto started_at = measurement_enabled_ ?
            std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        ModelOutput output = inference_.run(input);
        output.metadata = input.metadata;
        if (measurement_enabled_)
        {
            record_stage_metric(metrics_, input.metadata, started_at,
                std::chrono::steady_clock::now());
            output.metadata.enqueued_at = std::chrono::steady_clock::now();
        }

        if (!output_queue_.push(std::move(output)))
            break;
    }

    // 더 이상 ModelOutput이 들어오지 않음을 다음 Stage에 알린다.
    output_queue_.close();

    Logger::info("[InferenceThread] 종료");
}
