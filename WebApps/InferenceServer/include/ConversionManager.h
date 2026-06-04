#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace inference {

struct ModelConfig;

enum class ConversionStatus {
    Queued,
    Running,
    Completed,
    Failed
};

struct BuildOptions {
    std::string onnxPath;
    std::string enginePath;
    bool fp16 = true;
    std::string inputName = "input";
    int inputC = 3;
    int inputH = 224;
    int inputW = 224;
    int maxBatchSize = 8;
};

struct ConversionJob {
    std::string id;
    ConversionStatus status = ConversionStatus::Queued;
    int progress = 0;          // 0-100
    std::string message;       // current step description
    std::string error;
    std::string enginePath;
    size_t engineSizeBytes = 0;
    BuildOptions options;
};

class ConversionManager {
public:
    ConversionManager();
    ~ConversionManager();

    ConversionManager(const ConversionManager&) = delete;
    ConversionManager& operator=(const ConversionManager&) = delete;

    std::string submit(const BuildOptions& opts);
    ConversionJob getStatus(const std::string& jobId) const;
    std::vector<ConversionJob> listJobs() const;

private:
    void workerLoop();
    bool buildEngine(const BuildOptions& opts, std::function<void(int, const std::string&)> progress);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> running_{true};

    std::vector<BuildOptions> queue_;
    std::unordered_map<std::string, ConversionJob> jobs_;
    std::vector<std::string> jobOrder_;  // preserve submission order

    int nextId_ = 1;
};

} // namespace inference
