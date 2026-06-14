#include "../include/ThreadPool.h"

ThreadPool::ThreadPool(int numThreads)
{
    if (numThreads <= 0)
        numThreads = static_cast<int>(std::thread::hardware_concurrency());
    if (numThreads <= 0) numThreads = 4;  // fallback

    workers_.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i)
    {
        workers_.emplace_back([this]() {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this]() {
                        return stop_.load() || !tasks_.empty();
                    });
                    if (stop_.load() && tasks_.empty())
                        return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_.store(true);
    }
    cv_.notify_all();
    for (auto& w : workers_)
    {
        if (w.joinable())
            w.join();
    }
}
