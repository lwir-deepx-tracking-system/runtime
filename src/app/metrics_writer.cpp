#include "app/metrics_writer.hpp"
#include "app/run_directory.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <future>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
namespace
{
// 단계 식별자를 CSV의 기존 stage 문자열로 변환한다.
const char *stage_name(MetricStage stage)
{
    switch(stage)
    {
        case MetricStage::Camera:
            return "camera";
        case MetricStage::Detection:
            return "detection";
        case MetricStage::Tracking:
            return "tracking";
        case MetricStage::Control:
            return "control_submit";
    }
    return "unknown";
}
} // namespace

struct MetricsWriter::Impl
{
    enum class Kind
    {
        Activate,
        Stage,
        Detection,
        Flush
    };
    struct Event
    {
        Kind                                kind       = Kind::Stage;
        std::uint64_t                       generation = 0;
        int                                 model_id   = 0;
        MetricStage                         stage      = MetricStage::Camera;
        StageMetric                         metric;
        std::array<double, 4>               timing{};
        std::shared_ptr<std::promise<void>> barrier;
    };
    struct Files
    {
        std::ofstream metrics, detection;
    };
    fs::path                     directory;
    std::map<int, fs::path>      model_dirs;
    std::map<int, Files>         files;
    std::map<std::uint64_t, int> generations;
    std::mutex                   mutex, error_mutex;
    std::condition_variable      wake;
    std::deque<Event>            queue;
    bool                         closing    = false;
    bool                         discarding = false;
    std::atomic<bool>            failed{false};
    std::string                  error;
    std::thread                  worker;
    static constexpr std::size_t capacity = 16384;

    // 저장 실패의 첫 원인을 보존하고 애플리케이션 감시 루프에 알린다.
    void fail(const std::string &message) noexcept
    {
        try
        {
            std::lock_guard<std::mutex> lock(error_mutex);
            if(error.empty()) error = message;
        }
        catch(...)
        {
        }
        failed.store(true);
    }
    // 큐가 가득 차면 무음 누락 대신 저장 실패를 보고해 호출자가 종료할 수 있게 한다.
    bool submit(Event event) noexcept
    {
        if(failed.load()) return false;
        try
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(discarding) return false;
                if(closing)
                {
                    fail("Measurement submitted after close");
                    return false;
                }
                if(queue.size() >= capacity)
                {
                    fail("Measurement queue overflow");
                    return false;
                }
                queue.push_back(std::move(event));
            }
            wake.notify_one();
            return true;
        }
        catch(const std::exception &e)
        {
            fail(e.what());
        }
        catch(...)
        {
            fail("Measurement queue failure");
        }
        return false;
    }
    // 헤더는 비어 있는 파일에만 쓰고 재진입한 모델 파일은 append로 연다.
    Files &open(int id)
    {
        auto [it, inserted] = files.try_emplace(id);
        if(inserted)
        {
            const auto dir        = model_dirs.at(id);
            auto       initialize = [](std::ofstream &stream, const fs::path &path, const char *header)
            {
                const bool empty = !fs::exists(path) || fs::file_size(path) == 0;
                stream.exceptions(std::ios::failbit | std::ios::badbit);
                stream.open(path, std::ios::out | std::ios::app);
                stream << std::fixed << std::setprecision(6);
                if(empty) stream << header;
            };
            initialize(it->second.metrics, dir / "metrics.csv",
                       "frame_id,stage,queue_wait_ms,processing_ms,e2e_ms\n");
            initialize(it->second.detection, dir / "detection_timing.csv",
                       "frame_id,preprocess_ms,latency_ms,npu_processing_ms,postprocess_ms\n");
        }
        return it->second;
    }
    // 열린 모델 파일의 버퍼를 비우고 전환 경계에서는 파일도 닫는다.
    void flush(bool close_files = false)
    {
        for(auto &[id, value] : files)
        {
            value.metrics.flush();
            value.detection.flush();
            if(close_files)
            {
                value.metrics.close();
                value.detection.close();
            }
        }
        if(close_files) files.clear();
    }
    // 모델 전환 이벤트는 앞선 기록을 flush하고 세대→모델 연결을 확정한다.
    void write(const Event &event)
    {
        if(event.kind == Kind::Flush)
        {
            flush();
            event.barrier->set_value();
            return;
        }
        if(event.kind == Kind::Activate)
        {
            if(!model_dirs.count(event.model_id)) throw std::runtime_error("Unknown measurement model id");
            flush(true);
            generations[event.generation] = event.model_id;
            return;
        }
        auto &output = open(generations.at(event.generation));
        if(event.kind == Kind::Stage)
        {
            const auto &metric = event.metric;
            auto       &stream = output.metrics;
            stream << metric.frame_id << ',' << stage_name(event.stage) << ',';
            if(metric.queue_wait_ms) stream << *metric.queue_wait_ms;
            stream << ',' << metric.processing_ms << ',';
            if(metric.e2e_ms) stream << *metric.e2e_ms;
            stream << '\n';
        }
        else
        {
            output.detection << event.metric.frame_id;
            for(double value : event.timing) output.detection << ',' << value;
            output.detection << '\n';
        }
    }
    // 저장 전용 스레드에서 큐를 처리하고 주기적으로 버퍼를 비운다.
    void run() noexcept
    {
        try
        {
            auto     deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            unsigned count    = 0;
            for(;;)
            {
                Event event;
                bool  has_event = false;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait_until(lock, deadline, [&]
                                    { return closing || !queue.empty(); });
                    if(discarding) break;
                    if(!queue.empty())
                    {
                        event = std::move(queue.front());
                        queue.pop_front();
                        has_event = true;
                    }
                    else if(closing) break;
                }
                if(has_event)
                {
                    write(event);
                    ++count;
                }
                if(count >= 128 || std::chrono::steady_clock::now() >= deadline)
                {
                    flush();
                    count    = 0;
                    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(discarding)
                {
                    files.clear();
                    return;
                }
            }
            flush(true);
        }
        catch(const std::exception &e)
        {
            fail(e.what());
        }
        catch(...)
        {
            fail("Measurement writer failure");
        }
    }
};

MetricsWriter::MetricsWriter(const MetricsWriterOptions &options)
    : impl_(std::make_unique<Impl>())
{
    impl_->directory = create_run_directory(options.output_root);
    for(const auto &[source, relative] : options.snapshots)
    {
        if(relative.empty() || relative.is_absolute())
            throw std::invalid_argument("Snapshot destination must be relative");
        for(const auto &part : relative)
            if(part == "..") throw std::invalid_argument("Snapshot destination escapes run directory");
        const auto destination = impl_->directory / relative;
        fs::create_directories(destination.parent_path());
        fs::copy_file(source, destination);
    }
    for(const auto &[id, source] : options.model_configs)
    {
        const auto dir = impl_->directory / "models" / std::to_string(id);
        fs::create_directories(dir);
        fs::copy_file(source, dir / "model.yaml");
        impl_->model_dirs.emplace(id, dir);
    }
    impl_->worker = std::thread([this]
                                { impl_->run(); });
}
MetricsWriter::~MetricsWriter() { close(); }
void MetricsWriter::activate(std::uint64_t generation, int model_id) noexcept
{
    Impl::Event event;
    event.kind       = Impl::Kind::Activate;
    event.generation = generation;
    event.model_id   = model_id;
    impl_->submit(std::move(event));
}
void MetricsWriter::record(std::uint64_t generation, MetricStage stage, const StageMetric &metric) noexcept
{
    Impl::Event event;
    event.generation = generation;
    event.stage      = stage;
    event.metric     = metric;
    impl_->submit(std::move(event));
}
void MetricsWriter::record_detection(std::uint64_t generation, const DetectionTiming &timing) noexcept
{
    Impl::Event event;
    event.kind            = Impl::Kind::Detection;
    event.generation      = generation;
    event.metric.frame_id = timing.frame_id;
    event.timing          = {timing.preprocess_ms(), timing.latency_ms(), timing.npu_processing_ms(), timing.postprocess_ms()};
    impl_->submit(std::move(event));
}
void MetricsWriter::flush()
{
    Impl::Event event;
    event.kind     = Impl::Kind::Flush;
    event.barrier  = std::make_shared<std::promise<void>>();
    auto completed = event.barrier->get_future();
    if(!impl_->submit(std::move(event))) throw std::runtime_error(error_message());
    while(completed.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
        if(failed()) throw std::runtime_error(error_message());
    completed.get();
    if(failed()) throw std::runtime_error(error_message());
}
void MetricsWriter::close() noexcept
{
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->closing = true;
    }
    impl_->wake.notify_all();
    if(impl_->worker.joinable()) impl_->worker.join();
}
void MetricsWriter::discard()
{
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->discarding = true;
        impl_->closing    = true;
        impl_->queue.clear();
    }
    impl_->wake.notify_all();
    if(impl_->worker.joinable()) impl_->worker.join();
    // I/O 실패로 저장 스레드가 먼저 끝난 경우에도 핸들을 닫고 삭제한다.
    impl_->files.clear();
    fs::remove_all(impl_->directory);
}
bool        MetricsWriter::failed() const noexcept { return impl_->failed.load(); }
std::string MetricsWriter::error_message() const
{
    std::lock_guard<std::mutex> lock(impl_->error_mutex);
    return impl_->error;
}
const fs::path &MetricsWriter::directory() const { return impl_->directory; }
