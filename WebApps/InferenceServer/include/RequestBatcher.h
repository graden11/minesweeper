#pragma once

#include "ModelFactory.h"
#include "RequestSlotPool.h"

namespace http { struct PerfTrace; }  // fwd from HttpServer/include/http/HttpResponse.h

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class RequestBatcher
{
public:
    struct Item
    {
        std::string modelName;
        int inputWidth = 0;
        int inputHeight = 0;
        int inputChannels = 0;
        std::shared_ptr<RequestSlot> slot;          // pooled buffer (owns imageBytes + resultJson)
        std::shared_ptr<std::promise<std::string>> promise;
        std::chrono::steady_clock::time_point enqueueTime;
        std::shared_ptr<http::PerfTrace> perfTrace; // alias into slot->perfTrace
    };

    RequestBatcher(ModelFactory* factory, int maxBatchSize,
                   std::chrono::milliseconds maxDelayMs);
    ~RequestBatcher();

    void start();
    void stop();

    /// Submit a request using a pre-allocated slot.
    /// The slot->imageBytes must already be filled by the caller.
    /// slot->perfTrace is copied into the item for batcher-side checkpoints.
    std::future<std::string> submit(std::string modelName,
                                    std::shared_ptr<RequestSlot> slot,
                                    int inputW = 0, int inputH = 0, int inputC = 0);

private:
    void workerLoop();

    ModelFactory* factory_;
    int maxBatchSize_;
    std::chrono::milliseconds maxDelayMs_;

    static constexpr int kImmediateThreshold = 4;

    std::deque<Item> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::vector<std::future<void>> pendingFutures_;
};
