#include "pipeline/preprocess_thread.hpp"
#include "common/logger.hpp"
#include <utility>
#include <chrono>

// Preprocessor와 입력/출력 Queue를 Thread에 연결한다.
PreprocessThread::PreprocessThread(
    Preprocessor& preprocessor,
    ThreadSafeQueue<Frame>& input_queue,
    ThreadSafeQueue<ModelInput>& output_queue,
    bool measurement_enabled)
    : preprocessor_(preprocessor),
      input_queue_(input_queue),
      output_queue_(output_queue),
      measurement_enabled_(measurement_enabled)
{
}

// Preprocess Thread pthread를 생성하고 실행을 시작한다.
void PreprocessThread::start()
{
    pthread_create(
        &thread_,
        nullptr,
        &PreprocessThread::thread_func,
        this
    );
}

// Preprocess Thread thread가 종료될 때까지 기다린다.
void PreprocessThread::join()
{
    pthread_join(thread_, nullptr);
}

// pthread_create()에서 호출되는 정적 진입 함수.
// 전달받은 this 포인터를 복원한 뒤 실제 run()을 실행한다.
void* PreprocessThread::thread_func(void* arg)
{
    auto* Thread = static_cast<PreprocessThread*>(arg);

    Thread->run();

    return nullptr;
}

// 입력 Queue에서 Frame을 받아 전처리하고,
// 결과 ModelInput을 다음 Stage Queue로 전달한다.
void PreprocessThread::run()
{
    while (true)
    {
        Frame frame;

        // 이전 Stage에서 Frame 수신.
        // Queue가 close되고 남은 데이터가 없으면 Thread 종료.
        if (!input_queue_.pop(frame))
        {
            break;
        }

        // 선택된 Preprocessor 구현으로 전처리 수행
        const auto started_at = measurement_enabled_ ?
            std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        ModelInput input = preprocessor_.preprocess(frame);
        input.metadata = frame.metadata;
        if (measurement_enabled_)
        {
            record_stage_metric(metrics_, frame.metadata, started_at,
                std::chrono::steady_clock::now());
            input.metadata.enqueued_at = std::chrono::steady_clock::now();
        }

        // 다음 Inference Stage로 결과 전달
        if (!output_queue_.push(std::move(input)))
        {
            break;
        }
    }
    // 더 이상 ModelInput이 생성되지 않음을 다음 Stage에 알린다.
    output_queue_.close();

    Logger::info("[PreprocessThread] 종료");
}
