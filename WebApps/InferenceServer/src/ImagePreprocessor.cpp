#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include <stb_image_resize2.h>

#include "../include/ImagePreprocessor.h"

#include <algorithm>
#include <muduo/base/Logging.h>

namespace inference {

ImagePreprocessor::ImagePreprocessor(const ModelConfig& config)
    : targetW_(config.input.preferred_width),
      targetH_(config.input.preferred_height),
      targetC_(config.input.channels),
      hwcLayout_(config.input.layout == "hwc"),
      mean_(config.input.mean),
      std_(config.input.std)
{
}

std::vector<float> ImagePreprocessor::preprocess(const std::vector<uint8_t>& imageBytes)
{
    int w, h, channels;
    unsigned char* data = stbi_load_from_memory(
        imageBytes.data(), static_cast<int>(imageBytes.size()),
        &w, &h, &channels, targetC_);
    if (!data)
    {
        LOG_ERROR << "ImagePreprocessor: failed to decode image";
        return {};
    }

    int elemCount = targetC_ * targetH_ * targetW_;
    std::vector<float> input(elemCount);

    // uint8 srgb → uint8 linear resize via stb. Cannot eliminate this intermediate
    // buffer because stbir_resize_uint8_srgb outputs uint8 only; a direct
    // uint8→float resize with stbir_resize(generic) proved unreliable at value range.
    std::vector<uint8_t> resized(targetH_ * targetW_ * targetC_);
    stbir_resize_uint8_srgb(data, w, h, 0,
                            resized.data(), targetW_, targetH_, 0,
                            static_cast<stbir_pixel_layout>(targetC_));
    stbi_image_free(data);

    // Normalize + transpose in one pass.
    if (hwcLayout_) {
        for (int i = 0; i < elemCount; ++i) {
            int c = i % targetC_;
            float val = resized[i] / 255.0f;
            input[i] = (val - mean_[c]) / std_[c];
        }
    } else {
        // HWC source → CHW target: fused transpose + normalize.
        // Loop order y→x→c gives sequential reads from resized (stride=3 per pixel,
        // 3 consecutive bytes = 1 cache line), and plane-at-a-time sequential
        // writes to CHW output.
        std::vector<float> transposed(elemCount);
        for (int y = 0; y < targetH_; ++y) {
            for (int x = 0; x < targetW_; ++x) {
                int hwcBase = (y * targetW_ + x) * targetC_;
                for (int c = 0; c < targetC_; ++c) {
                    int chwIdx = (c * targetH_ + y) * targetW_ + x;
                    float val = resized[hwcBase + c] / 255.0f;
                    transposed[chwIdx] = (val - mean_[c]) / std_[c];
                }
            }
        }
        return transposed;
    }

    return input;
}

} // namespace inference
