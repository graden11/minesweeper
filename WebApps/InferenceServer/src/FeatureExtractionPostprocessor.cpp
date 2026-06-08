#include "../include/FeatureExtractionPostprocessor.h"

namespace inference {

nlohmann::json FeatureExtractionPostprocessor::postprocess(
    const InferenceOutput& output,
    const std::vector<std::string>& /*labels*/)
{
    nlohmann::json resp;
    resp["status"]    = "ok";
    resp["task_type"] = "feature_extraction";
    resp["dimension"] = static_cast<int>(output.totalElements());
    resp["embedding"] = std::vector<float>(output.dataPtrOrCopy(), output.dataPtrOrCopy() + output.totalElements());
    return resp;
}

} // namespace inference
