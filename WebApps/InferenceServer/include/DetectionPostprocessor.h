#pragma once

#include "Postprocessor.h"

namespace inference {

/// Detection postprocessor: NMS + bounding box decoding + JSON formatting.
class DetectionPostprocessor : public Postprocessor
{
public:
    explicit DetectionPostprocessor(const ModelConfig& config);

    nlohmann::json postprocess(const InferenceOutput& output,
                               const std::vector<std::string>& labels) override;

    std::vector<nlohmann::json> postprocessBatch(
        const InferenceOutput& batchOutput,
        int batchSize,
        const std::vector<std::string>& labels) override;

private:
    /// CPU-based Non-Maximum Suppression (IoU).
    /// Keeps detections where confidence >= minConfidence and iou < nmsThreshold.
    static std::vector<int> nms(const std::vector<float>& boxes,
                                const std::vector<float>& scores,
                                float nmsThreshold,
                                float minConfidence);

    /// Compute Intersection-over-Union for two axis-aligned boxes.
    static float iou(const float* a, const float* b);

    float confidenceThreshold_;
    float nmsThreshold_;
    int maxDetections_;
};

} // namespace inference
