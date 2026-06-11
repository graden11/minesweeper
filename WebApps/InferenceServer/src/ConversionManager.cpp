#include "../include/ConversionManager.h"
#include "../include/ModelConfig.h"

#include <NvInfer.h>
#include <NvOnnxParser.h>
#include <cuda_runtime.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>
#include <muduo/base/Logging.h>

namespace inference {

static std::string generateId()
{
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    const char* hex = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 16; ++i) id += hex[dist(rng)];
    return id;
}

// ---------------------------------------------------------------------------
// TensorRT Logger
// ---------------------------------------------------------------------------
class TrtLogger : public nvinfer1::ILogger
{
    void log(Severity severity, const char* msg) noexcept override
    {
        if (severity <= Severity::kWARNING)
            LOG_WARN << "[TensorRT] " << msg;
    }
};

// ---------------------------------------------------------------------------
// ConversionManager
// ---------------------------------------------------------------------------
ConversionManager::ConversionManager()
{
    worker_ = std::thread(&ConversionManager::workerLoop, this);
}

ConversionManager::~ConversionManager()
{
    running_ = false;
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

std::string ConversionManager::submit(const BuildOptions& opts)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = generateId();
    ConversionJob job;
    job.id = id;
    job.options = opts;
    job.enginePath = opts.enginePath;
    jobs_[id] = job;
    jobOrder_.push_back(id);
    queue_.push_back(opts);
    cv_.notify_one();
    LOG_INFO << "Conversion job queued: " << id << " (" << opts.onnxPath << " -> " << opts.enginePath << ")";
    return id;
}

ConversionJob ConversionManager::getStatus(const std::string& jobId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = jobs_.find(jobId);
    if (it != jobs_.end()) return it->second;
    ConversionJob j;
    j.status = ConversionStatus::Failed;
    j.error = "Job not found";
    return j;
}

std::vector<ConversionJob> ConversionManager::listJobs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ConversionJob> result;
    for (auto& id : jobOrder_) {
        auto it = jobs_.find(id);
        if (it != jobs_.end()) result.push_back(it->second);
    }
    return result;
}

void ConversionManager::workerLoop()
{
    while (running_)
    {
        BuildOptions opts;
        std::string jobId;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
            if (!running_) break;
            opts = queue_.front();
            queue_.erase(queue_.begin());
            // find matching job id
            for (auto& [id, job] : jobs_) {
                if (job.status == ConversionStatus::Queued && job.options.onnxPath == opts.onnxPath) {
                    jobId = id;
                    break;
                }
            }
        }

        if (jobId.empty()) continue;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_[jobId].status = ConversionStatus::Running;
            jobs_[jobId].message = "Building TensorRT engine...";
        }

        auto progress = [&](int pct, const std::string& msg) {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_[jobId].progress = pct;
            jobs_[jobId].message = msg;
        };

        progress(5, "Parsing ONNX model...");
        bool ok = buildEngine(opts, progress);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (ok) {
                jobs_[jobId].status = ConversionStatus::Completed;
                jobs_[jobId].progress = 100;
                jobs_[jobId].message = "Conversion complete";
                namespace fs = std::filesystem;
                jobs_[jobId].engineSizeBytes = fs::file_size(opts.enginePath);
                LOG_INFO << "Conversion job " << jobId << " completed: " << opts.enginePath;
            } else {
                jobs_[jobId].status = ConversionStatus::Failed;
                jobs_[jobId].error = jobs_[jobId].message;
                LOG_ERROR << "Conversion job " << jobId << " failed: " << jobs_[jobId].error;
            }
        }
    }
}

bool ConversionManager::buildEngine(const BuildOptions& opts, std::function<void(int, const std::string&)> progress)
{
    TrtLogger logger;
    auto builder = std::unique_ptr<nvinfer1::IBuilder>(nvinfer1::createInferBuilder(logger));
    if (!builder) {
        progress(0, "Failed to create TensorRT builder");
        return false;
    }
    const auto explicitBatch = 1U << static_cast<uint32_t>(nvinfer1::NetworkDefinitionCreationFlag::kEXPLICIT_BATCH);
    auto network = std::unique_ptr<nvinfer1::INetworkDefinition>(builder->createNetworkV2(explicitBatch));
    if (!network) { progress(0, "Failed to create network"); return false; }

    auto parser = std::unique_ptr<nvonnxparser::IParser>(nvonnxparser::createParser(*network, logger));
    if (!parser) { progress(0, "Failed to create ONNX parser"); return false; }

    progress(10, "Parsing ONNX model...");
    if (!parser->parseFromFile(opts.onnxPath.c_str(), static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
        progress(0, "Failed to parse ONNX: " + opts.onnxPath);
        return false;
    }
    auto trtConfig = std::unique_ptr<nvinfer1::IBuilderConfig>(builder->createBuilderConfig());
    trtConfig->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, 1ULL << 30);
    if (opts.fp16 && builder->platformHasFastFp16()) {
        trtConfig->setFlag(nvinfer1::BuilderFlag::kFP16);
        LOG_INFO << "TRT builder: FP16 enabled";
    }
    progress(30, "Building optimization profile...");
    int maxBatch = std::max(8, opts.maxBatchSize);

    // Auto-detect input name and dims from the parsed ONNX network
    auto* netInput = network->getInput(0);
    std::string inputName = opts.inputName;
    nvinfer1::Dims netDims{0};
    if (netInput) {
        inputName = netInput->getName();
        netDims = netInput->getDimensions();
        LOG_INFO << "TRT builder: auto-detected input '" << inputName
                 << "' nbDims=" << netDims.nbDims;
    } else {
        LOG_WARN << "TRT builder: no network input found, falling back to CLI input name '"
                 << opts.inputName << "'";
        netDims = nvinfer1::Dims4{1, opts.inputC, opts.inputH, opts.inputW};
    }

    bool isDynamicBatch = (netDims.nbDims >= 1 && netDims.d[0] == -1);

    // Read actual spatial/C dims from ONNX, not CLI
    auto makeProfileDims = [&](int batch) {
        nvinfer1::Dims4 d{};
        for (int i = 0; i < 4; ++i) {
            int dimVal = 1;
            if (i < netDims.nbDims && netDims.d[i] > 0)
                dimVal = static_cast<int>(netDims.d[i]);
            d.d[i] = (i == 0) ? batch : dimVal;
        }
        return d;
    };

    auto profile = builder->createOptimizationProfile();
    if (isDynamicBatch) {
        profile->setDimensions(inputName.c_str(), nvinfer1::OptProfileSelector::kMIN,
                               makeProfileDims(1));
        profile->setDimensions(inputName.c_str(), nvinfer1::OptProfileSelector::kOPT,
                               makeProfileDims(maxBatch / 2));
        profile->setDimensions(inputName.c_str(), nvinfer1::OptProfileSelector::kMAX,
                               makeProfileDims(maxBatch));
        LOG_INFO << "TRT builder: dynamic batch profile [" << 1 << ", " << maxBatch/2 << ", " << maxBatch << "]";
    } else {
        auto dims1 = makeProfileDims(1);
        profile->setDimensions(inputName.c_str(), nvinfer1::OptProfileSelector::kMIN, dims1);
        profile->setDimensions(inputName.c_str(), nvinfer1::OptProfileSelector::kOPT, dims1);
        profile->setDimensions(inputName.c_str(), nvinfer1::OptProfileSelector::kMAX, dims1);
        LOG_INFO << "TRT builder: static batch profile";
    }
    trtConfig->addOptimizationProfile(profile);
    progress(60, "Building serialized engine (may take minutes)...");
    auto plan = std::unique_ptr<nvinfer1::IHostMemory>(builder->buildSerializedNetwork(*network, *trtConfig));
    if (!plan) {
        progress(0, "Failed to build serialized TRT engine");
        return false;
    }
    progress(90, "Writing engine file...");
    std::ofstream out(opts.enginePath, std::ios::binary);
    if (!out) { progress(0, "Cannot write engine file: " + opts.enginePath); return false; }
    out.write(static_cast<const char*>(plan->data()), static_cast<std::streamsize>(plan->size()));
    if (!out) { progress(0, "Failed to write engine file"); return false; }
    LOG_INFO << "TRT engine built: " << opts.enginePath << " (" << plan->size() / 1024 / 1024 << " MB)";
    return true;
}

} // namespace inference
