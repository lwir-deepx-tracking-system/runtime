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

    void run()
    {
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || task_.valid(); });
                if (!task_.valid()) return;
                task = std::move(task_);
            }
            task();
        }
    }

public:
    ReidWorker() : thread_([this] { run(); }) {}
    ~ReidWorker()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_one();
        thread_.join();
    }

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
