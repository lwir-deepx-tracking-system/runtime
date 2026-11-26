#include "reid/reid_worker.hpp"

void ReidWorker::run()
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

ReidWorker::ReidWorker() : thread_([this] { run(); }) {}

ReidWorker::~ReidWorker()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_one();
    thread_.join();
}
