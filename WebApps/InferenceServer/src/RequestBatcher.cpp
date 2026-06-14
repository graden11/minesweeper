#include "../include/RequestBatcher.h"
#include "../include/InferenceEngine.h"

#include "../../../HttpServer/include/http/HttpResponse.h"
#include "../../../HttpServer/include/utils/MetricsCollector.h"

#include <muduo/base/Logging.h>
#include <future>

RequestBatcher::RequestBatcher(ModelFactory* factory, int maxBatchSize,
                               std::chrono::milliseconds maxDelayMs)
    : factory_(factory), maxBatchSize_(maxBatchSize), maxDelayMs_(maxDelayMs)
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
             << ", maxDelayMs=" << maxDelayMs_.count()
             << ", immediateThreshold=" << kImmediateThreshold;
}

void RequestBatcher::stop()
{
    running_.store(false);
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    // Drain pending futures so no task references freed memory after return
    for (auto& f : pendingFutures_)
        if (f.valid()) f.wait();
    pendingFutures_.clear();

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
                                               int inputW, int inputH, int inputC,
                                               std::shared_ptr<http::PerfTrace> perfTrace)
{
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        Item it{std::move(modelName), inputW, inputH, inputC,
                std::move(imageData), std::move(promise),
                std::chrono::steady_clock::now(), std::move(perfTrace)};
        // B0: enqueue time
        if (it.perfTrace)
            it.perfTrace->b0_enqueue = it.perfTrace->nowUs();
        queue_.push_back(std::move(it));
    }
    cv_.notify_one();

    return future;
}

void RequestBatcher::workerLoop()
{
    using namespace std::chrono;
    const auto maxDelayUs = duration_cast<microseconds>(maxDelayMs_);

    while (running_.load())
    {
        // Periodically prune completed futures to avoid unbounded growth
        pendingFutures_.erase(
            std::remove_if(pendingFutures_.begin(), pendingFutures_.end(),
                [](std::future<void>& f) {
                    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                }),
            pendingFutures_.end());

        std::vector<Item> batch;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });
            if (!running_.load() && queue_.empty())
                break;

            // ---- Queue-depth-driven dispatch ----
            // Two modes:
            //   IMMEDIATE (enoughQueued): drain all items up to maxBatchSize
            //                              without waiting.
            //   WAIT     (!enoughQueued):  collect items with a deadline of
            //                              now + maxDelayUs, waiting for more
            //                              arrivals until deadline expires.
            bool enoughQueued = (static_cast<int>(queue_.size()) >= kImmediateThreshold);

            if (enoughQueued)
            {
                // Drain up to maxBatchSize, no waiting
                while (!queue_.empty() && static_cast<int>(batch.size()) < maxBatchSize_)
                {
                    batch.push_back(std::move(queue_.front()));
                    queue_.pop_front();
                    if (batch.size() == 1 && batch[0].perfTrace)
                        batch[0].perfTrace->b1_pick_first = batch[0].perfTrace->nowUs();
                }
            }
            else
            {
                // Wait up to maxDelayUs to collect a decent batch
                auto deadline = steady_clock::now() + maxDelayUs;
                while (!queue_.empty() && static_cast<int>(batch.size()) < maxBatchSize_)
                {
                    batch.push_back(std::move(queue_.front()));
                    queue_.pop_front();
                    if (batch.size() == 1 && batch[0].perfTrace)
                        batch[0].perfTrace->b1_pick_first = batch[0].perfTrace->nowUs();

                    if (static_cast<int>(batch.size()) >= maxBatchSize_)
                        break;

                    auto now = steady_clock::now();
                    if (now >= deadline)
                        break;

                    if (queue_.empty())
                    {
                        auto remaining = duration_cast<microseconds>(deadline - now);
                        if (remaining <= microseconds(0))
                            break;
                        cv_.wait_for(lock, remaining,
                                     [this] { return !queue_.empty() || !running_.load(); });
                    }
                }
            }
        }

        if (batch.empty())
            continue;

        // B2: batch-collected time
        for (auto& it : batch)
            if (it.perfTrace) it.perfTrace->b2_batch_collected = it.perfTrace->nowUs();

        // Group by model name + input dimensions.
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

        // Fire-and-forget dispatch: move ownership into the async task so
        // the worker can immediately go back to collecting the next batch.
        // GPU mutex inside the engine serialises actual inference.
        for (auto& [groupKey, indices] : groups)
        {
            // Move per-item data out of batch (owned by the async task)
            struct TaskItem {
                std::shared_ptr<std::promise<std::string>> promise;
                std::shared_ptr<http::PerfTrace> perfTrace;
                std::vector<uint8_t> imageData;
                int64_t enqueueUs;
            };
            auto tasks = std::make_shared<std::vector<TaskItem>>();
            tasks->reserve(indices.size());
            for (auto idx : indices)
            {
                tasks->push_back({
                    std::move(batch[idx].promise),
                    std::move(batch[idx].perfTrace),
                    std::move(batch[idx].imageData),
                    duration_cast<microseconds>(batch[idx].enqueueTime.time_since_epoch()).count()
                });
            }
            std::string modelName = batch[indices[0]].modelName;
            int bs = static_cast<int>(indices.size());

            for (auto& t : *tasks)
                if (t.perfTrace) t.perfTrace->b3_group_dispatch = t.perfTrace->nowUs();

            // Fire-and-forget — future stored in pendingFutures_ to avoid
            // blocking on destruction. shared_ptr keeps TaskItem alive.
            pendingFutures_.push_back(
                std::async(std::launch::async, [this, tasks = std::move(tasks), modelName = std::move(modelName), bs]() {
                int64_t maxWait = 0;
                auto now = steady_clock::now();
                for (auto& t : *tasks)
                {
                    int64_t w = duration_cast<microseconds>(now.time_since_epoch()).count() - t.enqueueUs;
                    if (w > maxWait) maxWait = w;
                }
                MetricsCollector::instance().recordBatchMetrics(modelName, bs, maxWait);

                auto engine = factory_->getModel(modelName);
                if (!engine)
                {
                    std::string err = R"({"status":"error","message":"unknown model: )" + modelName + "\"}";
                    for (auto& t : *tasks)
                        t.promise->set_value(err);
                    return;
                }

                std::vector<std::vector<uint8_t>> images;
                images.reserve(tasks->size());
                for (auto& t : *tasks)
                    images.push_back(std::move(t.imageData));

                for (auto& t : *tasks)
                    if (t.perfTrace) t.perfTrace->b4_predict_begin = t.perfTrace->nowUs();

                std::vector<std::string> results;
                try
                {
                    results = engine->predictBatch(images);
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR << "predictBatch error: " << e.what();
                    std::string err = R"({"status":"error","message":")" + std::string(e.what()) + "\"}";
                    results.assign(tasks->size(), err);
                }

                for (auto& t : *tasks)
                    if (t.perfTrace) t.perfTrace->b5_predict_done = t.perfTrace->nowUs();

                for (size_t i = 0; i < tasks->size() && i < results.size(); ++i)
                {
                    (*tasks)[i].promise->set_value(std::move(results[i]));
                    if ((*tasks)[i].perfTrace) (*tasks)[i].perfTrace->b6_promise_set = (*tasks)[i].perfTrace->nowUs();
                }
            }));
        }
    }

    LOG_INFO << "RequestBatcher worker exited";
}
