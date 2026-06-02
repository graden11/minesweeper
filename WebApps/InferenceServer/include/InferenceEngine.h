#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Minimal pure-virtual interface for an inference engine.
/// ModelPipeline inherits this for seamless compatibility with ModelFactory,
/// handlers, and RequestBatcher — none of them need code changes.
class InferenceEngine
{
public:
    virtual ~InferenceEngine() = default;

    virtual std::string predict(const std::string& imagePath) = 0;
    virtual std::string predictFromBytes(const std::vector<uint8_t>& imageData) = 0;

    virtual int maxBatchSize() const { return 1; }

    virtual std::vector<std::string> predictBatch(const std::vector<std::vector<uint8_t>>& images)
    {
        std::vector<std::string> results;
        results.reserve(images.size());
        for (auto& img : images)
            results.push_back(predictFromBytes(img));
        return results;
    }
};
