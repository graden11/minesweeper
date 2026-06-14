#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/// Lightweight fixed-size thread pool for CPU-bound preprocessing.
///
/// Usage:
///   ThreadPool pool(4);
///   auto fut = pool.enqueue([&]{ return doWork(); });
///   auto result = fut.get();
class ThreadPool
{
public:
    /// @param numThreads  0 = std::thread::hardware_concurrency()
    explicit ThreadPool(int numThreads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Submit a callable; returns a future for the result.
    template<class F>
    auto enqueue(F&& f) -> std::future<decltype(f())>;

    int threadCount() const { return static_cast<int>(workers_.size()); }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
};

// ── template implementation ──────────────────────────────────────────

template<class F>
auto ThreadPool::enqueue(F&& f) -> std::future<decltype(f())>
{
    using R = decltype(f());
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
    auto fut = task->get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_.load())
            throw std::runtime_error("ThreadPool: enqueue on stopped pool");
        tasks_.emplace([task]() { (*task)(); });
    }
    cv_.notify_one();
    return fut;
}
