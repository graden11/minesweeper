#include "../include/Preprocessor.h"
#include "../include/ImagePreprocessor.h"
#include "../include/ModelConfig.h"

#include <fstream>
#include <iterator>
#include <muduo/base/Logging.h>

namespace inference {

std::vector<float> Preprocessor::preprocessFile(const std::string& imagePath)
{
    std::ifstream f(imagePath, std::ios::binary);
    if (!f)
    {
        LOG_ERROR << "Preprocessor::preprocessFile: cannot open: " << imagePath;
        return {};
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    return preprocess(bytes);
}

std::unique_ptr<Preprocessor> createPreprocessor(const ModelConfig& config)
{
    // For now, all tasks use ImagePreprocessor
    return std::make_unique<ImagePreprocessor>(config);
}

} // namespace inference
