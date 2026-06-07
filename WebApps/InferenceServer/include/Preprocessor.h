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
    /// Returns false on decode failure.
    virtual bool preprocess(const std::vector<uint8_t>& imageBytes,
                            std::vector<float>& output) = 0;

    /// Decode and preprocess, writing into batchOutput starting at the given
    /// float offset.  output.size() must be >= offset + elemCount.
    /// Default implementation calls preprocess()+copy; subclasses may fuse.
    virtual bool preprocessInto(const std::vector<uint8_t>& imageBytes,
                                std::vector<float>& batchOutput,
                                size_t offset)
    {
        thread_local std::vector<float> tmp;
        if (!preprocess(imageBytes, tmp))
            return false;
        std::copy(tmp.begin(), tmp.end(), batchOutput.begin() + offset);
        return true;
    }

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
