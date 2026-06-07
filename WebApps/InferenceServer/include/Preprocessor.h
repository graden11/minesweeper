#pragma once

#include "ModelConfig.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace inference {

/// Converts raw image bytes (JPEG / PNG / …) into a normalized float tensor.
class Preprocessor
{
public:
    virtual ~Preprocessor() = default;

    /// Decode and preprocess in-memory image bytes, filling a caller-owned buffer.
    /// Returns false on decode failure. Overrides the pure virtual below.
    virtual bool preprocess(const std::vector<uint8_t>& imageBytes,
                            std::vector<float>& output) = 0;

    /// Decode and preprocess in-memory image bytes → CHW float tensor.
    /// Convenience wrapper that allocates, kept for external callers.
    virtual std::vector<float> preprocess(const std::vector<uint8_t>& imageBytes)
    {
        std::vector<float> out;
        preprocess(imageBytes, out);
        return out;
    }

    /// Read an image file from disk, then delegate to preprocess().
    std::vector<float> preprocessFile(const std::string& imagePath);
};

/// Factory: creates the appropriate Preprocessor for a given ModelConfig.
std::unique_ptr<Preprocessor> createPreprocessor(const ModelConfig& config);

} // namespace inference
