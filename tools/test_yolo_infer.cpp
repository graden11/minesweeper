#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <vector>
#include <cstring>

int main() {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "test");
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);

    std::cout << "Loading model..." << std::endl;
    Ort::Session session(env, "/tmp/yolov8l.onnx", opts);

    auto info = session.GetInputTypeInfo(0);
    auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
    std::cout << "Input shape: [";
    for (size_t j=0;j<shape.size();j++){if(j)std::cout<<",";std::cout<<shape[j];}
    std::cout << "]" << std::endl;

    auto outInfo = session.GetOutputTypeInfo(0);
    auto outShape = outInfo.GetTensorTypeAndShapeInfo().GetShape();
    std::cout << "Output shape: [";
    for (size_t j=0;j<outShape.size();j++){if(j)std::cout<<",";std::cout<<outShape[j];}
    std::cout << "]" << std::endl;

    size_t inputSize = 1 * 3 * 640 * 640;
    std::vector<float> input(inputSize, 0.5f);

    std::vector<int64_t> inShape = {1, 3, 640, 640};
    Ort::MemoryInfo memInfo("Cpu", OrtArenaAllocator, 0, OrtMemTypeDefault);
    Ort::Value inTensor = Ort::Value::CreateTensor<float>(
        memInfo, input.data(), input.size(), inShape.data(), inShape.size());

    std::cout << "Running inference (" << inputSize*4/1024/1024 << " MB input)..." << std::endl;
    const char* inNames[] = {"images"};
    const char* outNames[] = {"output0"};

    try {
        auto outputs = session.Run(Ort::RunOptions{}, inNames, &inTensor, 1, outNames, 1);
        auto outTypeInfo = outputs[0].GetTensorTypeAndShapeInfo();
        auto outDims = outTypeInfo.GetShape();
        size_t outElems = outTypeInfo.GetElementCount();
        std::cout << "Output: [";
        for (size_t j=0;j<outDims.size();j++){if(j)std::cout<<",";std::cout<<outDims[j];}
        std::cout << "] " << outElems << " elems (" << outElems*4/1024/1024 << " MB)" << std::endl;

        float* data = outputs[0].GetTensorMutableData<float>();
        std::cout << "First 10 values: ";
        for (int i=0;i<10;i++) std::cout << data[i] << " ";
        std::cout << std::endl;
        std::cout << "SUCCESS" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
