#include "../include/RequestBatcher.h"
#include "../include/InferenceEngine.h"

#include "../../../HttpServer/include/utils/MetricsCollector.h"

#include <muduo/base/Logging.h>

RequestBatcher::RequestBatcher(ModelFactory* factory, int maxBatchSize,
                               std::chrono::milliseconds maxDelayMs)
    : factory_(factory), maxBatchSize_(maxBatchSize), maxDelayMs_(maxDelayMs),
      adaptiveDelayUs_(maxDelayMs)
{
}

RequestBatcher::~RequestBatcher()
{
    stop();
}

void RequestBatcher::start()
{
    running_.store(true);
    worker_ = std::thread(&RequestBatcher::workerLoop, this);
    LOG_INFO << "RequestBatcher started, maxBatchSize=" << maxBatchSize_
             << ", maxDelayMs=" << maxDelayMs_.count();
}

void RequestBatcher::stop()
{
    running_.store(false);
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty())
    {
        auto& item = queue_.front();
        item.promise->set_value(R"({"status":"error","message":"request cancelled: server shutting down"})");
        queue_.pop_front();
    }
}

std::future<std::string> RequestBatcher::submit(std::string modelName,
                                               std::vector<uint8_t> imageData,
                                               int inputW, int inputH, int inputC)
{
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({std::move(modelName), inputW, inputH, inputC,
                          std::move(imageData), std::move(promise),
                          std::chrono::steady_clock::now()});
    }
    cv_.notify_one();

    return future;
}

void RequestBatcher::workerLoop()
{
    while (running_.load())
    {
        std::vector<Item> batch;

        {
            std::chrono::microseconds delayUs;
            {
                std::lock_guard<std::mutex> lk(adaptiveMutex_);
                delayUs = adaptiveDelayUs_;
            }

            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });

            if (!running_.load() && queue_.empty())
                break;

            auto deadline = std::chrono::steady_clock::now() + delayUs;

            while (!queue_.empty() && static_cast<int>(batch.size()) < maxBatchSize_)
            {
                batch.push_back(std::move(queue_.front()));
                queue_.pop_front();

                if (static_cast<int>(batch.size()) >= maxBatchSize_)
                    break;

                auto now = std::chrono::steady_clock::now();
                if (now >= deadline)
                    break;

                auto remaining = deadline - now;
                cv_.wait_for(lock, remaining,
                             [this] { return !queue_.empty() || !running_.load(); });
            }
        }

        if (batch.empty())
            continue;

        // Adaptive timeout: exponentially weighted moving average of the time
        // it took to fill this batch.  Stays near maxDelayMs_ under low load
        // (merging-friendly), decreases under high load (latency-sensitive).
        {
            auto fillTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - batch[0].enqueueTime);
            std::lock_guard<std::mutex> lk(adaptiveMutex_);
            adaptiveDelayUs_ = std::chrono::microseconds(
                (adaptiveDelayUs_.count() * (kEmaAlpha - 1) + fillTime.count()) / kEmaAlpha);
            // Clamp to [1ms, configured max]
            if (adaptiveDelayUs_ < std::chrono::milliseconds(1))
                adaptiveDelayUs_ = std::chrono::milliseconds(1);
            if (adaptiveDelayUs_ > maxDelayMs_)
                adaptiveDelayUs_ = maxDelayMs_;
        }

        // Group by model name + input dimensions so that requests with
        // different resolutions are never merged into the same batch.
        auto now = std::chrono::steady_clock::now();
        std::unordered_map<std::string, std::vector<size_t>> groups;
        for (size_t i = 0; i < batch.size(); ++i)
        {
            auto& it = batch[i];
            std::string key = it.modelName + ":" +
                std::to_string(it.inputHeight) + "x" +
                std::to_string(it.inputWidth) + "x" +
                std::to_string(it.inputChannels);
            groups[key].push_back(i);
        }

        for (auto& [groupKey, indices] : groups)
        {
            int bs = static_cast<int>(indices.size());
            std::string modelName = batch[indices[0]].modelName;
            int64_t maxWait = 0;
            for (auto idx : indices)
            {
                int64_t wait = std::chrono::duration_cast<std::chrono::microseconds>(
                    now - batch[idx].enqueueTime).count();
                if (wait > maxWait) maxWait = wait;
            }
            MetricsCollector::instance().recordBatchMetrics(modelName, bs, maxWait);

            auto engine = factory_->getModel(modelName);
            if (!engine)
            {
                std::string err = R"({"status":"error","message":"unknown model: )" + modelName + "\"}";
                for (auto idx : indices)
                    batch[idx].promise->set_value(err);
                continue;
            }

            std::vector<std::vector<uint8_t>> images;
            images.reserve(indices.size());
            for (auto idx : indices)
                images.push_back(std::move(batch[idx].imageData));

            std::vector<std::string> results;
            try
            {
                results = engine->predictBatch(images);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR << "predictBatch error: " << e.what();
                std::string err = R"({"status":"error","message":")" + std::string(e.what()) + "\"}";
                results.assign(indices.size(), err);
            }

            for (size_t i = 0; i < indices.size() && i < results.size(); ++i)
                batch[indices[i]].promise->set_value(std::move(results[i]));
        }
    }

    LOG_INFO << "RequestBatcher worker exited";
}
