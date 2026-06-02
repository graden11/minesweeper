#include "../include/InferenceBackend.h"

namespace inference {

InferenceOutput InferenceBackend::inferMulti(const std::vector<float>& input,
                                              const std::vector<int64_t>& inputShape)
{
    std::vector<int64_t> outShape;
    auto outData = infer(input, inputShape, outShape);

    InferenceOutput io;
    io.data  = std::move(outData);
    io.shape = std::move(outShape);
    // tensors / shapes / names left empty → isMultiOutput() == false
    return io;
}

InferenceOutput InferenceBackend::inferBatchMulti(const std::vector<float>& batchInput,
                                                   const std::vector<int64_t>& batchShape)
{
    std::vector<int64_t> outShape;
    auto outData = inferBatch(batchInput, batchShape, outShape);

    InferenceOutput io;
    io.data  = std::move(outData);
    io.shape = std::move(outShape);
    return io;
}

std::vector<float> InferenceBackend::inferBatch(const std::vector<float>& batchInput,
                                                 const std::vector<int64_t>& batchShape,
                                                 std::vector<int64_t>& outputShape)
{
    // Default: sequential infer, splitting batch dim
    int batchSize = static_cast<int>(batchShape[0]);
    if (batchSize == 0) return {};

    // Per-sample input shape: {1, ...rest of dims}
    std::vector<int64_t> singleShape = batchShape;
    singleShape[0] = 1;
    size_t perSampleInput = 1;
    for (size_t i = 1; i < singleShape.size(); ++i)
        perSampleInput *= static_cast<size_t>(singleShape[i]);

    std::vector<float> allResults;
    for (int i = 0; i < batchSize; ++i)
    {
        std::vector<float> sampleInput(batchInput.begin() + i * perSampleInput,
                                        batchInput.begin() + (i + 1) * perSampleInput);
        std::vector<int64_t> sampleOutShape;
        auto sampleOut = infer(sampleInput, singleShape, sampleOutShape);
        allResults.insert(allResults.end(), sampleOut.begin(), sampleOut.end());

        if (i == 0)
        {
            outputShape = sampleOutShape;
            outputShape[0] = batchSize;
        }
    }
    return allResults;
}

} // namespace inference
