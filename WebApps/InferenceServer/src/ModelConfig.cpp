#include "../include/ModelConfig.h"

namespace inference {

ModelConfig ModelConfig::fromJson(const std::string& name,
                                   const std::string& version,
                                   const nlohmann::json& entry,
                                   const std::string& globalLabelsPath,
                                   int defaultBatchSize)
{
    ModelConfig cfg;
    cfg.name    = name;
    cfg.version = version.empty() ? "1" : version;
    cfg.type    = entry.value("type", "onnx");
    cfg.path    = entry.value("path", "");
    cfg.max_batch_size = entry.value("max_batch_size", defaultBatchSize);

    // Task
    cfg.task = parseTaskType(entry.value("task", "classification"));

    // Labels — per-model first, then global fallback
    cfg.labels_path = entry.value("labels", globalLabelsPath);
    cfg.top_k = entry.value("top_k", 5);

    // Input config (with ImageNet defaults)
    if (entry.contains("input"))
    {
        auto& in = entry["input"];
        cfg.input.name = in.value("name", "input");
        cfg.input.preferred_width  = in.value("width", 224);
        cfg.input.preferred_height = in.value("height", 224);
        cfg.input.channels         = in.value("channels", 3);

        if (in.contains("mean"))
        {
            cfg.input.mean.clear();
            for (auto& v : in["mean"])
                cfg.input.mean.push_back(v.get<float>());
        }
        if (in.contains("std"))
        {
            cfg.input.std.clear();
            for (auto& v : in["std"])
                cfg.input.std.push_back(v.get<float>());
        }

        // Explicit shape override
        if (in.contains("shape"))
        {
            cfg.input.shape.clear();
            for (auto& v : in["shape"])
                cfg.input.shape.push_back(v.get<int64_t>());
        }
    }
    else
    {
        cfg.input.name = entry.value("input_name", "input");
        cfg.input.preferred_width  = entry.value("input_width", 224);
        cfg.input.preferred_height = entry.value("input_height", 224);
        cfg.input.channels         = entry.value("input_channels", 3);
    }

    // Output config
    if (entry.contains("output"))
    {
        auto& out = entry["output"];
        cfg.output.name = out.value("name", "output");
        if (out.contains("shape"))
        {
            cfg.output.shape.clear();
            for (auto& v : out["shape"])
                cfg.output.shape.push_back(v.get<int64_t>());
        }
    }
    else
    {
        cfg.output.name = entry.value("output_name", "output");
    }

    return cfg;
}

ModelConfig ModelConfig::simple(const std::string& name,
                                 const std::string& version,
                                 const std::string& type,
                                 const std::string& path,
                                 const std::string& labelsPath,
                                 int batchSize)
{
    ModelConfig cfg;
    cfg.name    = name;
    cfg.version = version.empty() ? "1" : version;
    cfg.type    = type;
    cfg.path    = path;
    cfg.labels_path = labelsPath;
    cfg.max_batch_size = batchSize;
    // All other fields use ImageNet defaults
    return cfg;
}

} // namespace inference
