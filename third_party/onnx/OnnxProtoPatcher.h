// OnnxProtoPatcher.h — make ONNX batch dimension dynamic by patching the protobuf.
// Used before TensorRT engine building so that all TRT engines get dynamic batching,
// regardless of how the ONNX was originally exported.
#pragma once
#include <string>

namespace inference {

/// Returns true if any dim was actually changed.
/// Creates a .bak backup unless it already exists.
bool patchOnnxBatchDim(const std::string& onnxPath);

} // namespace inference
