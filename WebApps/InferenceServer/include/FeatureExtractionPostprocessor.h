#pragma once

#include "Postprocessor.h"

namespace inference {

class FeatureExtractionPostprocessor : public Postprocessor
{
public:
    nlohmann::json postprocess(const InferenceOutput& output,
                               const std::vector<std::string>& labels) override;
};

} // namespace inference
