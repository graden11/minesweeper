#pragma once

#include "InferenceEngine.h"

#include <memory>
#include <string>
#include <unordered_map>

class ModelFactory
{
public:
    void registerModel(const std::string &name, std::unique_ptr<InferenceEngine> engine);
    InferenceEngine* getModel(const std::string &name) const;
    bool hasModel(const std::string &name) const;

private:
    std::unordered_map<std::string, std::unique_ptr<InferenceEngine>> models_;
};
