#include "ThreadPool.h"

namespace util {

ThreadPool::ThreadPool(size_t numThreads) {
    if (numThreads == 0) numThreads = 1;
    workers_.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock lk(mutex_);
                    cv_.wait(lk, [this]{ return stop_ || !queue_.empty(); });
                    if (stop_ && queue_.empty()) return;
                    task = std::move(queue_.front());
                    queue_.pop();
                    ++active_;
                }
                task();
                {
                    std::lock_guard lk(mutex_);
                    --active_;
                }
                cvIdle_.notify_all();
            }
        });
    }
}

ThreadPool::~ThreadPool() { stop(); }

void ThreadPool::stop() {
    {
        std::lock_guard lk(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_)
        if (w.joinable()) w.join();
}

void ThreadPool::waitAll() {
    std::unique_lock lk(mutex_);
    cvIdle_.wait(lk, [this]{ return queue_.empty() && active_ == 0; });
}

} // namespace util
