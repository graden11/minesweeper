// OnnxProtoPatcher.cpp
#include "OnnxProtoPatcher.h"
#include "onnx.proto.min.pb.h"

#include <filesystem>
#include <fstream>
#include <muduo/base/Logging.h>

namespace inference {

bool patchOnnxBatchDim(const std::string& onnxPath)
{
    // ── Read raw bytes ────────────────────────────────────────────
    std::ifstream in(onnxPath, std::ios::binary | std::ios::ate);
    if (!in) {
        LOG_ERROR << "OnnxProtoPatcher: cannot open " << onnxPath;
        return false;
    }
    auto size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::string raw(static_cast<size_t>(size), '\0');
    in.read(raw.data(), size);
    if (!in) {
        LOG_ERROR << "OnnxProtoPatcher: read failed " << onnxPath;
        return false;
    }
    in.close();

    // ── Parse ──────────────────────────────────────────────────────
    onnx::ModelProto model;
    if (!model.ParseFromString(raw)) {
        LOG_ERROR << "OnnxProtoPatcher: protobuf parse failed for " << onnxPath;
        return false;
    }

    if (!model.has_graph()) {
        LOG_WARN << "OnnxProtoPatcher: no graph in " << onnxPath;
        return false;
    }

    auto patchDim = [](onnx::TensorShapeProto* shape) -> bool {
        if (!shape || shape->dim_size() < 1)
            return false;
        auto* d = shape->mutable_dim(0);
        // already dynamic → nothing to do
        if (d->has_dim_param() && !d->dim_param().empty())
            return false;
        int64_t oldValue = d->has_dim_value() ? d->dim_value() : -1;
        d->set_dim_param("batch");
        d->clear_dim_value();
        LOG_INFO << "OnnxProtoPatcher: dim[0] " << oldValue << " → dynamic (dim_param='batch')";
        return true;
    };

    bool changed = false;
    auto* graph = model.mutable_graph();

    for (int i = 0; i < graph->input_size(); ++i) {
        auto* vi = graph->mutable_input(i);
        if (vi->has_type() && vi->type().has_tensor_type()) {
            auto* tensor = vi->mutable_type()->mutable_tensor_type();
            if (tensor->has_shape()) {
                if (patchDim(tensor->mutable_shape()))
                    changed = true;
            }
        }
    }
    for (int i = 0; i < graph->output_size(); ++i) {
        auto* vi = graph->mutable_output(i);
        if (vi->has_type() && vi->type().has_tensor_type()) {
            auto* tensor = vi->mutable_type()->mutable_tensor_type();
            if (tensor->has_shape()) {
                if (patchDim(tensor->mutable_shape()))
                    changed = true;
            }
        }
    }

    if (!changed) {
        LOG_INFO << "OnnxProtoPatcher: already dynamic, skipping " << onnxPath;
        return false;
    }

    // ── Backup (once) ──────────────────────────────────────────────
    std::string bakPath = onnxPath + ".bak";
    if (!std::filesystem::exists(bakPath)) {
        std::filesystem::copy_file(onnxPath, bakPath);
        LOG_INFO << "OnnxProtoPatcher: backup → " << bakPath;
    }

    // ── Serialize ──────────────────────────────────────────────────
    std::string outBytes;
    if (!model.SerializeToString(&outBytes)) {
        LOG_ERROR << "OnnxProtoPatcher: serialize failed for " << onnxPath;
        return false;
    }

    std::ofstream out(onnxPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        LOG_ERROR << "OnnxProtoPatcher: cannot write " << onnxPath;
        return false;
    }
    out.write(outBytes.data(), static_cast<std::streamsize>(outBytes.size()));
    if (!out) {
        LOG_ERROR << "OnnxProtoPatcher: write failed " << onnxPath;
        return false;
    }
    LOG_INFO << "OnnxProtoPatcher: patched → " << onnxPath;
    return true;
}

} // namespace inference
