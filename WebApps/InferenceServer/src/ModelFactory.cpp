#include "../include/ModelFactory.h"

void ModelFactory::registerModel(const std::string &name, std::unique_ptr<InferenceEngine> engine)
{
    models_[name] = std::move(engine);
}

InferenceEngine* ModelFactory::getModel(const std::string &name) const
{
    auto it = models_.find(name);
    if (it != models_.end())
        return it->second.get();
    return nullptr;
}

bool ModelFactory::hasModel(const std::string &name) const
{
    return models_.find(name) != models_.end();
}
