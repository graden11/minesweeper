#pragma once

#include "Preprocessor.h"

namespace inference {

/// Image preprocessor that decodes JPEG/PNG via stb_image,
/// resizes, normalizes, and outputs CHW float tensor.
class ImagePreprocessor : public Preprocessor
{
public:
    explicit ImagePreprocessor(const ModelConfig& config);

    bool preprocess(const std::vector<uint8_t>& imageBytes,
                    std::vector<float>& output) override;

    /// Fused: decode + resize + normalize into batchOutput at the given float offset.
    bool preprocessInto(const std::vector<uint8_t>& imageBytes,
                        std::vector<float>& batchOutput,
                        size_t offset) override;

private:
    int targetW_, targetH_, targetC_;
    bool hwcLayout_;
    std::vector<float> mean_;
    std::vector<float> std_;
    std::vector<float> scale_;
    std::vector<float> bias_;
};

} // namespace inference
