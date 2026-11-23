#include "reid/reid_worker.hpp"

#include <atomic>

int main()
{
    std::atomic<bool> drained{false};
    std::future<void> last;
    {
        ReidWorker worker;
        if (worker.submit([] { return std::this_thread::get_id(); }).get() ==
            std::this_thread::get_id()) return 1;
        try {
            worker.submit([] { throw std::runtime_error("test failure"); }).get();
            return 2;
        } catch (const std::runtime_error&) {}
        last = worker.submit([&] { drained = true; });
    }
    last.get();
    return drained ? 0 : 3;
}
