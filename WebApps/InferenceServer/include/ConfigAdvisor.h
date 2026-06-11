#pragma once

#include "../../../HttpServer/include/utils/ConfigLoader.h"
#include "HardwareDetector.h"
#include "ModelFactory.h"

/// Safety boundaries shared by all profiles.
namespace ConfigBounds {
    constexpr int MIN_OS_VRAM_MB     = 512;
    constexpr int MAX_WORKSPACE_MB   = 4096;
    constexpr int MIN_WORKSPACE_MB   = 256;
    constexpr int MAX_THREADS        = 32;
    constexpr int MIN_THREADS        = 2;
    constexpr int MAX_BATCH_SIZE     = 64;
    constexpr int MIN_BATCH_SIZE     = 4;
}

class ConfigAdvisor
{
public:
    /// Compute stable + aggressive recommendations from hardware profile and
    /// the currently loaded models. Writes results into cfg.recommendations
    /// and persists the full config back to disk.
    static void analyze(AppConfig &cfg,
                        const HardwareProfile &hw,
                        const ModelFactory *modelFactory,
                        const std::string &configPath);

private:
    /// Clamp `value` into [lo, hi]. Templated to work with int/size_t.
    template<typename T>
    static T clamp(T value, T lo, T hi)
    {
        if (value < lo) return lo;
        if (value > hi) return hi;
        return value;
    }

    /// Run the scenario and compute both profiles.
    static void compute(const HardwareProfile &hw, const ModelFactory *mf,
                        AppConfig &cfg);

    /// Compute one profile (stable or aggressive).
    static RecommendationProfile buildProfile(
        const std::string &key,
        const HardwareProfile &hw,
        const std::string &scenario,
        size_t freeVramMb,
        bool fp16Avail);
};
