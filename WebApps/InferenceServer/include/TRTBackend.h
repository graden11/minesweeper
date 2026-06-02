#pragma once

#include "InferenceBackend.h"

#include <NvInfer.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace inference {

/// TensorRT backend — pure tensor-in, tensor-out with CUDA.
class TRTBackend : public InferenceBackend
{
public:
    explicit TRTBackend(const ModelConfig& config);
    ~TRTBackend() override;

    std::vector<float> infer(const std::vector<float>& input,
                             const std::vector<int64_t>& inputShape,
                             std::vector<int64_t>& outputShape) override;

    std::vector<float> inferBatch(const std::vector<float>& batchInput,
                                  const std::vector<int64_t>& batchShape,
                                  std::vector<int64_t>& outputShape) override;

    int  maxBatchSize() const override { return maxBatchSize_; }
    bool isReady() const override { return context_ != nullptr; }

private:
    static constexpr int kNumSlots = 2;

    struct BufferSlot {
        float* h_input_pinned  = nullptr;
        void*  d_input          = nullptr;
        void*  d_output         = nullptr;
        float* h_output_pinned  = nullptr;
        bool   in_use           = false;
    };

    int  acquireSlot();
    void releaseSlot(int idx);

    bool loadEngine(const std::string& enginePath);
    static bool buildEngine(const std::string& onnxPath,
                            const std::string& enginePath,
                            const ModelConfig& config);

    void allocateBatchBuffers();

    class Logger : public nvinfer1::ILogger
    {
        void log(Severity severity, const char* msg) noexcept override;
    };

    // --- config-derived ---
    ModelConfig config_;
    int maxBatchSize_{1};

    // --- TensorRT objects ---
    Logger logger_;
    std::unique_ptr<nvinfer1::IRuntime> runtime_;
    std::unique_ptr<nvinfer1::ICudaEngine> engine_;
    std::unique_ptr<nvinfer1::IExecutionContext> context_;

    size_t inputSize_{0};
    size_t outputSize_{0};
    int inputIndex_{-1};
    int outputIndex_{-1};

    // --- CUDA ---
    cudaStream_t stream_{};
    std::mutex gpu_mutex_;

    // Double-buffered slots for single requests
    BufferSlot slots_[kNumSlots];
    std::mutex slot_pool_mutex_;
    std::condition_variable slot_pool_cv_;

    // Batch buffers
    float* h_batch_input_{nullptr};
    float* h_batch_output_{nullptr};
    void*  d_batch_input_{nullptr};
    void*  d_batch_output_{nullptr};
    size_t perSampleInputSize_{0};
    size_t perSampleOutputSize_{0};
};

} // namespace inference
