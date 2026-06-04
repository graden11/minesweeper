#include "../include/FeatureExtractionPostprocessor.h"

namespace inference {

nlohmann::json FeatureExtractionPostprocessor::postprocess(
    const InferenceOutput& output,
    const std::vector<std::string>& /*labels*/)
{
    nlohmann::json resp;
    resp["status"]    = "ok";
    resp["task_type"] = "feature_extraction";
    resp["dimension"] = static_cast<int>(output.data.size());
    resp["embedding"] = output.data;
    return resp;
}

} // namespace inference
