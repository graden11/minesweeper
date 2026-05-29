#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include <stb_image_resize2.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <NvInfer.h>
#include <NvOnnxParser.h>

#include <cuda_runtime.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

// ImageNet normalization (same as ResNet50Engine)
static constexpr float MEAN[3] = {0.485f, 0.456f, 0.406f};
static constexpr float STD[3] = {0.229f, 0.224f, 0.225f};
static constexpr int INPUT_W = 224;
static constexpr int INPUT_H = 224;
static constexpr int INPUT_C = 3;

class Logger : public nvinfer1::ILogger
{
    void log(Severity severity, const char *msg) noexcept override
    {
        if (severity <= Severity::kWARNING)
            std::cerr << "[TRT] " << msg << std::endl;
    }
};

static std::vector<float> preprocessImage(const std::string &path)
{
    int w, h, c;
    unsigned char *data = stbi_load(path.c_str(), &w, &h, &c, 3);
    if (!data)
        return {};

    std::vector<uint8_t> resized(INPUT_H * INPUT_W * INPUT_C);
    stbir_resize_uint8_srgb(data, w, h, 0, resized.data(),
                            INPUT_W, INPUT_H, 0,
                            static_cast<stbir_pixel_layout>(INPUT_C));
    stbi_image_free(data);

    std::vector<float> input(INPUT_C * INPUT_H * INPUT_W);
    for (int cc = 0; cc < INPUT_C; ++cc)
        for (int y = 0; y < INPUT_H; ++y)
            for (int x = 0; x < INPUT_W; ++x)
            {
                float val = resized[(y * INPUT_W + x) * INPUT_C + cc] / 255.0f;
                input[(cc * INPUT_H + y) * INPUT_W + x] = (val - MEAN[cc]) / STD[cc];
            }

    return input;
}

class Int8Calibrator : public nvinfer1::IInt8Calibrator
{
public:
    Int8Calibrator(const std::string &calibDir, const std::string &cacheFile)
        : cacheFile_(cacheFile)
    {
        // Collect all .jpg and .jpeg files from calibration directory
        DIR *dir = opendir(calibDir.c_str());
        if (!dir)
        {
            std::cerr << "Cannot open calibration directory: " << calibDir << std::endl;
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            std::string name(entry->d_name);
            if (name.size() > 4 &&
                (name.rfind(".jpg") == name.size() - 4 ||
                 name.rfind(".jpeg") == name.size() - 5))
            {
                paths_.push_back(calibDir + "/" + name);
            }
        }
        closedir(dir);

        std::cout << "Found " << paths_.size() << " calibration images" << std::endl;

        size_t inputBytes = INPUT_C * INPUT_H * INPUT_W * sizeof(float);
        cudaMallocHost(&hostBuffer_, inputBytes);
        cudaMalloc(&deviceBuffer_, inputBytes);
    }

    ~Int8Calibrator()
    {
        if (hostBuffer_)
            cudaFreeHost(hostBuffer_);
        if (deviceBuffer_)
            cudaFree(deviceBuffer_);
    }

    int32_t getBatchSize() const noexcept override { return 1; }

    bool getBatch(void *bindings[], char const * /*names*/[],
                  int32_t /*nbBindings*/) noexcept override
    {
        if (batchIdx_ >= static_cast<int>(paths_.size()))
            return false;

        auto input = preprocessImage(paths_[batchIdx_]);
        ++batchIdx_;

        if (input.empty())
            return getBatch(bindings, nullptr, 0); // skip broken images

        std::memcpy(hostBuffer_, input.data(), input.size() * sizeof(float));
        cudaMemcpy(deviceBuffer_, hostBuffer_,
                   input.size() * sizeof(float), cudaMemcpyHostToDevice);
        bindings[0] = deviceBuffer_;

        return true;
    }

    void const *readCalibrationCache(std::size_t &length) noexcept override
    {
        std::ifstream f(cacheFile_, std::ios::binary);
        if (!f.good())
            return nullptr;

        f.seekg(0, std::ios::end);
        length = f.tellg();
        f.seekg(0, std::ios::beg);

        char *buf = new char[length];
        f.read(buf, length);
        return buf;
    }

    void writeCalibrationCache(void const *ptr, std::size_t length) noexcept override
    {
        std::ofstream f(cacheFile_, std::ios::binary);
        f.write(static_cast<char const *>(ptr), length);
        std::cout << "Calibration cache saved to " << cacheFile_
                  << " (" << length << " bytes)" << std::endl;
    }

    nvinfer1::CalibrationAlgoType getAlgorithm() noexcept override
    {
        return nvinfer1::CalibrationAlgoType::kENTROPY_CALIBRATION_2;
    }

    nvinfer1::InterfaceInfo getInterfaceInfo() const noexcept override
    {
        return {"IInt8Calibrator", 1, 0};
    }

private:
    std::vector<std::string> paths_;
    std::string cacheFile_;
    float *hostBuffer_{nullptr};
    float *deviceBuffer_{nullptr};
    int batchIdx_{0};
};

static void printUsage()
{
    std::cout << "Usage: convert onnx_path engine_path [--fp16] [--int8]"
              << " [--calib-dir DIR] [--calib-cache FILE]\n"
              << "  --fp16       Enable FP16 precision (default)\n"
              << "  --int8       Enable INT8 precision (requires --calib-dir)\n"
              << "  --calib-dir  Directory with calibration images (required for --int8)\n"
              << "  --calib-cache Cache file path (default: calib_cache.bin)\n";
}

int main(int argc, char *argv[])
{
    // Parse args
    if (argc < 3)
    {
        printUsage();
        return 1;
    }

    const char *onnxPath = argv[1];
    const char *enginePath = argv[2];
    bool useFP16 = false;
    bool useINT8 = false;
    std::string calibDir;
    std::string calibCache = "calib_cache.bin";

    for (int i = 3; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--fp16") == 0)
            useFP16 = true;
        else if (std::strcmp(argv[i], "--int8") == 0)
            useINT8 = true;
        else if (std::strcmp(argv[i], "--calib-dir") == 0 && i + 1 < argc)
            calibDir = argv[++i];
        else if (std::strcmp(argv[i], "--calib-cache") == 0 && i + 1 < argc)
            calibCache = argv[++i];
    }

    // Default to FP16 if neither specified
    if (!useFP16 && !useINT8)
        useFP16 = true;

    if (useINT8 && calibDir.empty())
    {
        std::cerr << "--int8 requires --calib-dir" << std::endl;
        return 1;
    }

    // Init CUDA
    cudaFree(nullptr);

    Logger logger;
    auto builder = std::unique_ptr<nvinfer1::IBuilder>(nvinfer1::createInferBuilder(logger));
    if (!builder)
    {
        std::cerr << "Failed to create builder" << std::endl;
        return 1;
    }

    auto network = std::unique_ptr<nvinfer1::INetworkDefinition>(
        builder->createNetworkV2(1 << static_cast<int>(
            nvinfer1::NetworkDefinitionCreationFlag::kEXPLICIT_BATCH)));
    auto parser = std::unique_ptr<nvonnxparser::IParser>(
        nvonnxparser::createParser(*network, logger));

    std::cout << "Parsing ONNX: " << onnxPath << std::endl;
    if (!parser->parseFromFile(onnxPath,
        static_cast<int>(nvinfer1::ILogger::Severity::kWARNING)))
    {
        for (int i = 0; i < parser->getNbErrors(); ++i)
            std::cerr << "  " << parser->getError(i)->desc() << std::endl;
        return 1;
    }

    auto config = std::unique_ptr<nvinfer1::IBuilderConfig>(builder->createBuilderConfig());
    config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, 1ULL << 30);

    // Optimization profile
    if (network->getNbInputs() > 0)
    {
        auto profile = builder->createOptimizationProfile();
        auto input = network->getInput(0);
        auto shape = input->getDimensions();

        nvinfer1::Dims minDims, optDims, maxDims;
        minDims.nbDims = shape.nbDims;
        optDims.nbDims = shape.nbDims;
        maxDims.nbDims = shape.nbDims;
        for (int32_t d = 0; d < shape.nbDims; ++d)
        {
            minDims.d[d] = (d == 0) ? 1 : (shape.d[d] > 0 ? shape.d[d] : 1);
            optDims.d[d] = (d == 0) ? 1 : (shape.d[d] > 0 ? shape.d[d] : 224);
            maxDims.d[d] = (d == 0) ? 1 : (shape.d[d] > 0 ? shape.d[d] : 256);
        }
        profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kMIN, minDims);
        profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kOPT, optDims);
        profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kMAX, maxDims);
        config->addOptimizationProfile(profile);
    }

    // INT8 calibrator
    std::unique_ptr<Int8Calibrator> calibrator;
    if (useINT8)
    {
        calibrator = std::make_unique<Int8Calibrator>(calibDir, calibCache);
        config->setFlag(nvinfer1::BuilderFlag::kINT8);
        config->setInt8Calibrator(calibrator.get());
        std::cout << "INT8 calibration enabled (" << calibDir << ")" << std::endl;
    }

    if (useFP16)
    {
        config->setFlag(nvinfer1::BuilderFlag::kFP16);
        std::cout << "FP16 enabled" << std::endl;
    }

    std::cout << "Building engine (may take several minutes)..." << std::endl;
    auto serialized = std::unique_ptr<nvinfer1::IHostMemory>(
        builder->buildSerializedNetwork(*network, *config));
    if (!serialized)
    {
        std::cerr << "Failed to build engine" << std::endl;
        return 1;
    }

    std::ofstream out(enginePath, std::ios::binary);
    out.write(static_cast<const char *>(serialized->data()), serialized->size());
    out.close();

    std::cout << "Engine written to " << enginePath
              << " (" << serialized->size() / (1024 * 1024) << " MB)" << std::endl;
    return 0;
}

#pragma GCC diagnostic pop
