#pragma once

#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

// One waiting frame per tracker, with a single executing worker. Destruction drains the worker.
class ReidWorker {
    using Task = std::packaged_task<void()>;
    std::mutex mutex_;
    std::condition_variable ready_;
    Task task_;
    bool stopping_ = false;
    std::thread thread_;

    void run();

public:
    ReidWorker();
    ~ReidWorker();

    template<class Work>
    auto submit(Work work)
    {
        std::packaged_task<decltype(work())()> result(std::move(work));
        auto future = result.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (task_.valid()) throw std::logic_error("ReID worker already has a frame");
            task_ = Task([result = std::move(result)]() mutable { result(); });
        }
        ready_.notify_one();
        return future;
    }
};
