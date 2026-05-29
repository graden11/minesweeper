#pragma once

#include "InferenceEngine.h"

#include <onnxruntime_cxx_api.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

class ResNet50Engine : public InferenceEngine
{
public:
    ResNet50Engine(const std::string &modelPath, const std::string &labelsPath);
    ~ResNet50Engine() override;

    std::string predict(const std::string &imagePath) override;
    std::string predictFromBytes(const std::vector<uint8_t> &imageData) override;

private:
    std::vector<float> preprocess(const uint8_t *rgbData, int w, int h, int channels);
    std::vector<std::pair<int, float>> runInference(const std::vector<float> &inputTensor);

    Ort::Env env_;
    Ort::SessionOptions sessionOpts_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memInfo_;
    std::vector<std::string> labels_;
};
