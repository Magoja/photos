#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>
#include <vector>
#include <stdexcept>

namespace util {

class ThreadPool {
 public:
  explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
  ~ThreadPool();

  template <class F, class... Args>
  auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using R = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<R()>>(
      [f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable {
        return f(std::forward<Args>(args)...);
      });
    auto fut = task->get_future();
    {
      std::lock_guard lk(mutex_);
      if (stop_)
        throw std::runtime_error("ThreadPool is stopped");
      queue_.emplace([task] { (*task)(); });
    }
    cv_.notify_one();
    return fut;
  }

  void waitAll();
  void stop();
  size_t numThreads() const { return workers_.size(); }

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
  int active_ = 0;
  std::condition_variable cvIdle_;
};

}  // namespace util
