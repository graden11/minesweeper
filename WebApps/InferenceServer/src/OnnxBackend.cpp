#include "../include/OnnxBackend.h"

#include <muduo/base/Logging.h>

namespace inference {

OnnxBackend::OnnxBackend(const ModelConfig& config)
    : env_(ORT_LOGGING_LEVEL_WARNING, "onnx"),
      memInfo_("Cpu", OrtArenaAllocator, 0, OrtMemTypeDefault),
      inputName_(config.input.name),
      outputName_(config.output.name),
      maxBatchSize_(config.max_batch_size)
{
    sessionOpts_.SetIntraOpNumThreads(1);
    session_ = std::make_unique<Ort::Session>(env_, config.path.c_str(), sessionOpts_);
    LOG_INFO << "OnnxBackend initialized, model: " << config.path
             << ", maxBatchSize: " << maxBatchSize_;
}

std::vector<float> OnnxBackend::infer(const std::vector<float>& input,
                                       const std::vector<int64_t>& inputShape,
                                       std::vector<int64_t>& outputShape)
{
    Ort::Value inputValue = Ort::Value::CreateTensor<float>(
        memInfo_,
        const_cast<float*>(input.data()),
        input.size(),
        inputShape.data(),
        inputShape.size());

    const char* inputNames[]  = {inputName_.c_str()};
    const char* outputNames[] = {outputName_.c_str()};

    std::lock_guard<std::mutex> lock(inferenceMutex_);
    auto outputValues = session_->Run(Ort::RunOptions{},
                                      inputNames, &inputValue, 1,
                                      outputNames, 1);

    float* logits = outputValues[0].GetTensorMutableData<float>();
    auto typeInfo = outputValues[0].GetTensorTypeAndShapeInfo();
    auto outShape = typeInfo.GetShape();
    size_t elemCount = typeInfo.GetElementCount();

    outputShape.assign(outShape.begin(), outShape.end());
    return std::vector<float>(logits, logits + elemCount);
}

std::vector<float> OnnxBackend::inferBatch(const std::vector<float>& batchInput,
                                            const std::vector<int64_t>& batchShape,
                                            std::vector<int64_t>& outputShape)
{
    Ort::Value inputValue = Ort::Value::CreateTensor<float>(
        memInfo_,
        const_cast<float*>(batchInput.data()),
        batchInput.size(),
        batchShape.data(),
        batchShape.size());

    const char* inputNames[]  = {inputName_.c_str()};
    const char* outputNames[] = {outputName_.c_str()};

    std::lock_guard<std::mutex> lock(inferenceMutex_);
    auto outputValues = session_->Run(Ort::RunOptions{},
                                      inputNames, &inputValue, 1,
                                      outputNames, 1);

    float* logits = outputValues[0].GetTensorMutableData<float>();
    auto typeInfo = outputValues[0].GetTensorTypeAndShapeInfo();
    auto outShape = typeInfo.GetShape();
    size_t elemCount = typeInfo.GetElementCount();

    outputShape.assign(outShape.begin(), outShape.end());
    return std::vector<float>(logits, logits + elemCount);
}

} // namespace inference
