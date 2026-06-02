#pragma once

#include "InferenceBackend.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace inference {

/// Singleton registry that maps backend type strings ("onnx", "tensorrt", …)
/// to factory functions.  Adding a new backend requires one registration line.
class BackendRegistry
{
public:
    using FactoryFunc = std::function<std::unique_ptr<InferenceBackend>(const ModelConfig&)>;

    static BackendRegistry& instance();

    void registerBackend(const std::string& type, FactoryFunc factory);
    std::unique_ptr<InferenceBackend> create(const std::string& type, const ModelConfig& config);

    bool has(const std::string& type) const;
    std::vector<std::string> list() const;

private:
    BackendRegistry() = default;
    std::unordered_map<std::string, FactoryFunc> factories_;
};

} // namespace inference
