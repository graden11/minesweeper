#pragma once

#include <string>
#include <vector>

/// Hardware profile produced by detection layer.
struct HardwareProfile
{
    // CPU
    int cpu_logical_cores  = 0;
    int cpu_physical_cores = 0;
    size_t total_ram_gb    = 0;

    // GPU
    int gpu_count = 0;
    struct GpuInfo {
        std::string name;
        size_t total_memory_mb = 0;
        size_t free_memory_mb  = 0;
        int cc_major = 0;
        int cc_minor = 0;
        int sm_count = 0;
        bool fp16_supported = false;   // compute capability >= 7.0
    };
    std::vector<GpuInfo> gpus;

    // Model scenario helpers (filled by ConfigAdvisor)
    bool has_cpu_models = false;
    bool has_gpu_models = false;
    float max_per_sample_mb = 0.0f;

    bool hasGpu() const { return gpu_count > 0; }
};

class HardwareDetector
{
public:
    /// Detect CPU, RAM, and (on TensorRT builds) GPU hardware.
    /// Returns false on critical failure (e.g. unable to determine core count).
    static bool detect(HardwareProfile &out);

private:
    static bool detectCpu(HardwareProfile &out);
    static bool detectRam(HardwareProfile &out);
    static bool detectGpu(HardwareProfile &out);
};
