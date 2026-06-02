#pragma once

#include "InferenceBackend.h"

#include <onnxruntime_cxx_api.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace inference {

/// ONNX Runtime backend — pure tensor-in, tensor-out.
class OnnxBackend : public InferenceBackend
{
public:
    explicit OnnxBackend(const ModelConfig& config);
    ~OnnxBackend() override = default;

    std::vector<float> infer(const std::vector<float>& input,
                             const std::vector<int64_t>& inputShape,
                             std::vector<int64_t>& outputShape) override;

    std::vector<float> inferBatch(const std::vector<float>& batchInput,
                                  const std::vector<int64_t>& batchShape,
                                  std::vector<int64_t>& outputShape) override;

    int  maxBatchSize() const override { return maxBatchSize_; }
    bool isReady() const override { return session_ != nullptr; }

private:
    Ort::Env env_;
    Ort::SessionOptions sessionOpts_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memInfo_;

    std::string inputName_;
    std::string outputName_;
    int maxBatchSize_;
    std::mutex inferenceMutex_;
};

} // namespace inference
