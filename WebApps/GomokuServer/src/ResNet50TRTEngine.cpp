#include <stb_image.h>
#include <stb_image_resize2.h>

#include "../include/ResNet50TRTEngine.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <muduo/base/Logging.h>

#include "../../../HttpServer/include/utils/JsonUtil.h"

void ResNet50TRTEngine::Logger::log(Severity severity, const char *msg) noexcept
{
    if (severity <= Severity::kWARNING)
        LOG_WARN << "[TensorRT] " << msg;
}

static bool initCuda()
{
    cudaError_t err = cudaFree(nullptr);
    return err == cudaSuccess;
}

ResNet50TRTEngine::ResNet50TRTEngine(const std::string &enginePath, const std::string &labelsPath)
{
    if (!initCuda())
    {
        LOG_ERROR << "CUDA initialization failed";
        return;
    }

    labels_ = loadLabels(labelsPath);

    if (!loadEngine(enginePath))
    {
        LOG_ERROR << "Failed to load TensorRT engine: " << enginePath;
        return;
    }

    // Create CUDA stream for async execution
    cudaError_t err = cudaStreamCreate(&stream_);
    if (err != cudaSuccess)
        LOG_ERROR << "cudaStreamCreate failed: " << cudaGetErrorString(err);

    // Allocate double-buffered pinned + device memory slots
    size_t resizeBytes = INPUT_H * INPUT_W * INPUT_C * sizeof(uint8_t);
    for (int i = 0; i < kNumSlots; ++i)
    {
        BufferSlot &s = slots_[i];
        cudaMallocHost(&s.h_resized_pinned, resizeBytes);
        cudaMallocHost(&s.h_input_pinned, inputSize_);
        cudaMallocHost(&s.h_output_pinned, outputSize_);
        cudaMalloc(&s.d_input, inputSize_);
        cudaMalloc(&s.d_output, outputSize_);
    }

    LOG_INFO << "ResNet50TRTEngine initialized, engine: " << enginePath
             << ", labels: " << labels_.size()
             << ", stream + " << kNumSlots << " pinned-memory slots";
}

ResNet50TRTEngine::~ResNet50TRTEngine()
{
    cudaStreamDestroy(stream_);
    for (int i = 0; i < kNumSlots; ++i)
    {
        BufferSlot &s = slots_[i];
        if (s.h_resized_pinned) cudaFreeHost(s.h_resized_pinned);
        if (s.h_input_pinned)   cudaFreeHost(s.h_input_pinned);
        if (s.h_output_pinned)  cudaFreeHost(s.h_output_pinned);
        if (s.d_input)          cudaFree(s.d_input);
        if (s.d_output)         cudaFree(s.d_output);
    }
}

int ResNet50TRTEngine::acquireSlot()
{
    std::unique_lock<std::mutex> lock(slot_pool_mutex_);
    slot_pool_cv_.wait(lock, [this] {
        for (int i = 0; i < kNumSlots; ++i)
            if (!slots_[i].in_use) return true;
        return false;
    });
    for (int i = 0; i < kNumSlots; ++i)
    {
        if (!slots_[i].in_use)
        {
            slots_[i].in_use = true;
            return i;
        }
    }
    return -1; // unreachable
}

void ResNet50TRTEngine::releaseSlot(int idx)
{
    {
        std::lock_guard<std::mutex> lock(slot_pool_mutex_);
        slots_[idx].in_use = false;
    }
    slot_pool_cv_.notify_one();
}

bool ResNet50TRTEngine::loadEngine(const std::string &enginePath)
{
    std::ifstream file(enginePath, std::ios::binary);
    if (!file)
    {
        LOG_ERROR << "Cannot open engine file: " << enginePath;
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> engineData(size);
    file.read(engineData.data(), size);

    runtime_.reset(nvinfer1::createInferRuntime(logger_));
    if (!runtime_)
    {
        LOG_ERROR << "Failed to create TensorRT runtime";
        return false;
    }

    engine_.reset(runtime_->deserializeCudaEngine(engineData.data(), size));
    if (!engine_)
    {
        LOG_ERROR << "Failed to deserialize CUDA engine";
        return false;
    }

    context_.reset(engine_->createExecutionContext());
    if (!context_)
    {
        LOG_ERROR << "Failed to create execution context";
        return false;
    }

    // Find input/output binding info
    int nbTensors = engine_->getNbIOTensors();
    for (int i = 0; i < nbTensors; i++)
    {
        const char *name = engine_->getIOTensorName(i);
        auto mode = engine_->getTensorIOMode(name);
        auto dims = engine_->getTensorShape(name);
        size_t tensorSize = sizeof(float);
        for (int d = 0; d < dims.nbDims; d++)
            tensorSize *= static_cast<size_t>(dims.d[d]);

        if (mode == nvinfer1::TensorIOMode::kINPUT)
        {
            inputIndex_ = i;
            inputSize_ = tensorSize;
        }
        else
        {
            outputIndex_ = i;
            outputSize_ = tensorSize;
        }
    }

    return true;
}

void ResNet50TRTEngine::preprocess(const uint8_t *rgbData, int w, int h, int channels,
                                    uint8_t *h_resized_pinned, float *h_input_pinned)
{
    stbir_resize_uint8_srgb(rgbData, w, h, 0,
                            h_resized_pinned, INPUT_W, INPUT_H, 0,
                            static_cast<stbir_pixel_layout>(INPUT_C));

    const int pixels = INPUT_H * INPUT_W;
    for (int c = 0; c < INPUT_C; ++c)
    {
        const float inv_std = 1.0f / STD[c];
        float *dst = h_input_pinned + c * pixels;
        for (int p = 0; p < pixels; ++p)
        {
            float val = h_resized_pinned[p * INPUT_C + c] / 255.0f;
            dst[p] = (val - MEAN[c]) * inv_std;
        }
    }
}

void ResNet50TRTEngine::runInference(int slotIdx,
                                      std::vector<std::pair<int, float>> &results)
{
    BufferSlot &slot = slots_[slotIdx];

    // GPU section — serialized across threads
    {
        std::lock_guard<std::mutex> lock(gpu_mutex_);

        cudaMemcpyAsync(slot.d_input, slot.h_input_pinned,
                        inputSize_, cudaMemcpyHostToDevice, stream_);

        context_->setInputTensorAddress(INPUT_NAME, slot.d_input);
        context_->setOutputTensorAddress(OUTPUT_NAME, slot.d_output);
        context_->enqueueV3(stream_);

        cudaMemcpyAsync(slot.h_output_pinned, slot.d_output,
                        outputSize_, cudaMemcpyDeviceToHost, stream_);

        cudaStreamSynchronize(stream_);
    }

    // CPU post-processing — no lock needed, per-slot output already ready
    size_t numClasses = outputSize_ / sizeof(float);

    results = softmaxTopK(slot.h_output_pinned, numClasses);
}

std::string ResNet50TRTEngine::predict(const std::string &imagePath)
{
    if (!context_)
    {
        json err;
        err["status"] = "error";
        err["message"] = "TensorRT engine not loaded";
        return err.dump();
    }

    int slotIdx = acquireSlot();

    int w, h, channels;
    unsigned char *data = stbi_load(imagePath.c_str(), &w, &h, &channels, 3);
    if (!data)
    {
        LOG_ERROR << "Failed to load image: " << imagePath;
        releaseSlot(slotIdx);
        json err;
        err["status"] = "error";
        err["message"] = std::string("failed to load image: ") + imagePath;
        return err.dump();
    }

    preprocess(data, w, h, 3,
               slots_[slotIdx].h_resized_pinned,
               slots_[slotIdx].h_input_pinned);
    stbi_image_free(data);

    std::vector<std::pair<int, float>> results;
    runInference(slotIdx, results);

    releaseSlot(slotIdx);

    return buildPredictionsJson(results, labels_).dump();
}

std::string ResNet50TRTEngine::predictFromBytes(const std::vector<uint8_t> &imageData)
{
    if (!context_)
    {
        json err;
        err["status"] = "error";
        err["message"] = "TensorRT engine not loaded";
        return err.dump();
    }

    int slotIdx = acquireSlot();

    int w, h, channels;
    unsigned char *data = stbi_load_from_memory(
        imageData.data(), static_cast<int>(imageData.size()),
        &w, &h, &channels, 3);
    if (!data)
    {
        LOG_ERROR << "Failed to decode image from memory";
        releaseSlot(slotIdx);
        json err;
        err["status"] = "error";
        err["message"] = "failed to decode image from memory";
        return err.dump();
    }

    preprocess(data, w, h, 3,
               slots_[slotIdx].h_resized_pinned,
               slots_[slotIdx].h_input_pinned);
    stbi_image_free(data);

    std::vector<std::pair<int, float>> results;
    runInference(slotIdx, results);

    releaseSlot(slotIdx);

    return buildPredictionsJson(results, labels_).dump();
}
