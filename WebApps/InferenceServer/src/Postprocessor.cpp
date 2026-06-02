#include "../include/Postprocessor.h"
#include "../include/ClassificationPostprocessor.h"
#include "../include/DetectionPostprocessor.h"
#include "../include/SegmentationPostprocessor.h"
#include "../include/ModelConfig.h"

namespace inference {

nlohmann::json Postprocessor::postprocess(const std::vector<float>& output,
                                           const std::vector<int64_t>& outputShape,
                                           const std::vector<std::string>& labels)
{
    InferenceOutput io;
    io.data  = output;
    io.shape = outputShape;
    return postprocess(io, labels);
}

std::vector<nlohmann::json> Postprocessor::postprocessBatch(
    const InferenceOutput& batchOutput,
    int batchSize,
    const std::vector<std::string>& labels)
{
    // Default: split evenly by outputShape[1] (works for classification)
    std::vector<nlohmann::json> results;
    results.reserve(batchSize);

    size_t perSampleOut = batchOutput.shape.size() >= 2
        ? static_cast<size_t>(batchOutput.shape[1])
        : batchOutput.data.size() / batchSize;

    for (int i = 0; i < batchSize; ++i)
    {
        auto begin = batchOutput.data.begin() + i * perSampleOut;
        auto end   = begin + perSampleOut;
        std::vector<float> sampleOut(begin, end);

        InferenceOutput sampleIO;
        sampleIO.data  = std::move(sampleOut);
        sampleIO.shape = {1, static_cast<int64_t>(perSampleOut)};

        results.push_back(postprocess(sampleIO, labels));
    }
    return results;
}

std::unique_ptr<Postprocessor> createPostprocessor(const ModelConfig& config)
{
    switch (config.task)
    {
        case TaskType::CLASSIFICATION:
            return std::make_unique<ClassificationPostprocessor>(config.top_k);
        case TaskType::DETECTION:
            return std::make_unique<DetectionPostprocessor>(config);
        case TaskType::SEGMENTATION:
            return std::make_unique<SegmentationPostprocessor>(config);
    }
    // fallback
    return std::make_unique<ClassificationPostprocessor>(config.top_k);
}

} // namespace inference
