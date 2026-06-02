#include "../include/DetectionPostprocessor.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace inference {

DetectionPostprocessor::DetectionPostprocessor(const ModelConfig& config)
    : confidenceThreshold_(config.confidence_threshold),
      nmsThreshold_(config.nms_threshold),
      maxDetections_(config.max_detections)
{
}

float DetectionPostprocessor::iou(const float* a, const float* b)
{
    // a, b = {x1, y1, x2, y2}
    float interX1 = std::max(a[0], b[0]);
    float interY1 = std::max(a[1], b[1]);
    float interX2 = std::min(a[2], b[2]);
    float interY2 = std::min(a[3], b[3]);

    float interW = std::max(0.0f, interX2 - interX1);
    float interH = std::max(0.0f, interY2 - interY1);
    float interArea = interW * interH;

    float areaA = (a[2] - a[0]) * (a[3] - a[1]);
    float areaB = (b[2] - b[0]) * (b[3] - b[1]);
    float unionArea = areaA + areaB - interArea;

    return (unionArea > 0.0f) ? interArea / unionArea : 0.0f;
}

std::vector<int> DetectionPostprocessor::nms(const std::vector<float>& boxes,
                                              const std::vector<float>& scores,
                                              float nmsThreshold,
                                              float minConfidence)
{
    int numBoxes = static_cast<int>(scores.size());

    // Sort indices by score descending
    std::vector<int> indices(numBoxes);
    for (int i = 0; i < numBoxes; ++i) indices[i] = i;
    std::sort(indices.begin(), indices.end(),
              [&](int a, int b) { return scores[a] > scores[b]; });

    std::vector<int> keep;
    keep.reserve(numBoxes);

    for (int i = 0; i < numBoxes; ++i)
    {
        int idx = indices[i];
        if (scores[idx] < minConfidence) continue;

        bool suppressed = false;
        for (int k : keep)
        {
            if (iou(&boxes[idx * 4], &boxes[k * 4]) >= nmsThreshold)
            {
                suppressed = true;
                break;
            }
        }
        if (!suppressed)
            keep.push_back(idx);
    }

    return keep;
}

nlohmann::json DetectionPostprocessor::postprocess(
    const InferenceOutput& output,
    const std::vector<std::string>& labels)
{
    // Expect YOLO-style output: [1, 84, 8400] or [1, N, 6] (with boxes+scores+class_id)
    // Generic handling: treat output as [1, num_dets, num_fields]
    // where num_fields = 4 (bbox) + num_classes (scores) + optional extra

    // For now, accept a flexible format:
    //   - If output.shapes is non-empty (multi-output): use tensors[0] for raw output
    //   - Otherwise: use output.data flat

    const float* raw = output.data.data();
    size_t total = output.totalElements();

    if (total == 0)
    {
        nlohmann::json resp;
        resp["status"] = "ok";
        resp["task_type"] = "detection";
        resp["summary"] = "检测完成，未发现对象";
        resp["detections"] = nlohmann::json::array();
        return resp;
    }

    // Heuristic: if shape is {1, num_fields, num_anchors} → YOLO format
    //            if shape is {1, N, 6} → [x1,y1,x2,y2,conf,class_id] format
    size_t numFields = 0;
    size_t numAnchors = 0;
    if (output.shape.size() >= 3)
    {
        numFields  = static_cast<size_t>(output.shape[1]); // 84 for YOLOv8n
        numAnchors = static_cast<size_t>(output.shape[2]); // 8400 for YOLOv8n
    }
    else if (output.shape.size() >= 2)
    {
        numFields  = static_cast<size_t>(output.shape[1]); // 6 or 85
        numAnchors = 1;
    }
    else
    {
        // Flat output — assume N dets × 6
        numAnchors = total / 6;
        numFields  = 6;
    }

    size_t numClasses = (numFields > 5) ? (numFields - 4) : 1;

    // Collect boxes, scores, class_ids above threshold
    struct Detection { float x1, y1, x2, y2; float score; int classId; };
    std::vector<Detection> candidates;

    std::vector<float> boxesBuf;
    std::vector<float> scoresBuf;
    boxesBuf.reserve(numAnchors * 4);
    scoresBuf.reserve(numAnchors);

    for (size_t a = 0; a < numAnchors; ++a)
    {
        const float* entry = raw + a * numFields;

        float maxScore = 0.0f;
        int bestClass = 0;
        for (size_t c = 0; c < numClasses; ++c)
        {
            float s = entry[4 + c];
            if (s > maxScore) { maxScore = s; bestClass = static_cast<int>(c); }
        }

        if (maxScore < confidenceThreshold_) continue;

        Detection det;
        det.x1 = entry[0];
        det.y1 = entry[1];
        det.x2 = entry[2];
        det.y2 = entry[3];
        det.score   = maxScore;
        det.classId = bestClass;
        candidates.push_back(det);

        boxesBuf.insert(boxesBuf.end(), {det.x1, det.y1, det.x2, det.y2});
        scoresBuf.push_back(maxScore);
    }

    // NMS
    auto keep = nms(boxesBuf, scoresBuf, nmsThreshold_, confidenceThreshold_);

    // Keep top maxDetections
    if (static_cast<int>(keep.size()) > maxDetections_)
    {
        std::sort(keep.begin(), keep.end(),
                  [&](int a, int b) { return scoresBuf[a] > scoresBuf[b]; });
        keep.resize(maxDetections_);
    }

    // Build JSON
    nlohmann::json detections = nlohmann::json::array();
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(1);

    for (size_t i = 0; i < keep.size(); ++i)
    {
        auto& d = candidates[keep[i]];
        const std::string& label = (d.classId < static_cast<int>(labels.size()))
            ? labels[d.classId] : "unknown";

        nlohmann::json det;
        det["class_id"]   = d.classId;
        det["label"]      = label;
        det["confidence"] = d.score * 100.0f;
        det["bbox"]["x1"] = d.x1;
        det["bbox"]["y1"] = d.y1;
        det["bbox"]["x2"] = d.x2;
        det["bbox"]["y2"] = d.y2;
        detections.push_back(det);

        if (i == 0)
            summary << "检测到 " << label << "（" << (d.score * 100.0f) << "%）";
        else if (i == 1)
            summary << "、" << label << "（" << (d.score * 100.0f) << "%）";
    }

    if (keep.empty())
        summary << "检测完成，未发现对象";
    else
        summary << "，共 " << keep.size() << " 个对象";

    nlohmann::json resp;
    resp["status"]     = "ok";
    resp["task_type"]  = "detection";
    resp["summary"]    = summary.str();
    resp["detections"] = detections;
    return resp;
}

std::vector<nlohmann::json> DetectionPostprocessor::postprocessBatch(
    const InferenceOutput& batchOutput,
    int batchSize,
    const std::vector<std::string>& labels)
{
    // Detection batch postprocess: split by batch dim
    std::vector<nlohmann::json> results;
    results.reserve(batchSize);

    size_t numFields = 0;
    size_t numAnchors = 0;
    if (batchOutput.shape.size() >= 3)
    {
        numFields  = static_cast<size_t>(batchOutput.shape[1]);
        numAnchors = static_cast<size_t>(batchOutput.shape[2]);
    }
    else
    {
        // fallback: sequential single-sample
        for (int i = 0; i < batchSize; ++i)
        {
            InferenceOutput singleIO;
            singleIO.data  = batchOutput.data; // reference to entire data — FIXME: slice
            singleIO.shape = batchOutput.shape;
            results.push_back(postprocess(singleIO, labels));
        }
        return results;
    }

    size_t perSampleTotal = numFields * numAnchors;
    for (int i = 0; i < batchSize; ++i)
    {
        auto begin = batchOutput.data.begin() + i * perSampleTotal;
        auto end   = begin + perSampleTotal;

        InferenceOutput singleIO;
        singleIO.data.assign(begin, end);
        singleIO.shape = {1, static_cast<int64_t>(numFields), static_cast<int64_t>(numAnchors)};

        results.push_back(postprocess(singleIO, labels));
    }

    return results;
}

} // namespace inference
