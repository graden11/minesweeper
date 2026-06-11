#include "../include/HardwareDetector.h"

#include <unistd.h>
#include <spdlog/spdlog.h>

#ifdef ENABLE_TENSORRT
#include <cuda_runtime.h>
#endif

bool HardwareDetector::detect(HardwareProfile &out)
{
    if (!detectCpu(out))
    {
        spdlog::error("HardwareDetector: failed to detect CPU");
        return false;
    }
    if (!detectRam(out))
    {
        spdlog::warn("HardwareDetector: failed to detect RAM, assuming 0");
    }
    if (!detectGpu(out))
    {
        spdlog::warn("HardwareDetector: GPU detection skipped or failed");
    }

    spdlog::info("HardwareDetector: CPU={} logical cores ({} physical), RAM={} GB, GPU count={}",
                 out.cpu_logical_cores, out.cpu_physical_cores,
                 out.total_ram_gb, out.gpu_count);
    return true;
}

bool HardwareDetector::detectCpu(HardwareProfile &out)
{
    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    if (nproc <= 0)
    {
        spdlog::warn("sysconf(_SC_NPROCESSORS_ONLN) returned {}, trying fallback", nproc);
        nproc = 1;
    }
    out.cpu_logical_cores  = static_cast<int>(nproc);
    out.cpu_physical_cores = static_cast<int>(nproc);  // conservative — container may not expose topology
    return true;
}

bool HardwareDetector::detectRam(HardwareProfile &out)
{
    long pages  = sysconf(_SC_PHYS_PAGES);
    long pgsize = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || pgsize <= 0)
    {
        out.total_ram_gb = 0;
        return false;
    }
    out.total_ram_gb = static_cast<size_t>((pages * pgsize) / (1024LL * 1024 * 1024));
    return true;
}

bool HardwareDetector::detectGpu(HardwareProfile &out)
{
#ifdef ENABLE_TENSORRT
    int count = 0;
    cudaError_t err = cudaGetDeviceCount(&count);
    if (err != cudaSuccess)
    {
        spdlog::warn("cudaGetDeviceCount failed: {} (err={})", cudaGetErrorString(err), err);
        out.gpu_count = 0;
        return false;
    }
    out.gpu_count = count;

    for (int i = 0; i < count; ++i)
    {
        cudaDeviceProp prop{};
        err = cudaGetDeviceProperties(&prop, i);
        if (err != cudaSuccess)
        {
            spdlog::warn("cudaGetDeviceProperties({}) failed: {}", i, cudaGetErrorString(err));
            continue;
        }

        size_t freeBytes  = 0;
        size_t totalBytes = 0;
        // Set device first so cudaMemGetInfo queries the right GPU
        cudaSetDevice(i);
        cudaError_t memErr = cudaMemGetInfo(&freeBytes, &totalBytes);

        HardwareProfile::GpuInfo info;
        info.name            = prop.name;
        info.total_memory_mb = prop.totalGlobalMem / (1024 * 1024);
        info.free_memory_mb  = (memErr == cudaSuccess) ? freeBytes / (1024 * 1024) : info.total_memory_mb;
        info.cc_major        = prop.major;
        info.cc_minor        = prop.minor;
        info.sm_count        = prop.multiProcessorCount;
        info.fp16_supported  = (prop.major >= 7);

        out.gpus.push_back(info);

        spdlog::info("GPU[{}]: {} ({}MB total, {}MB free, CC {}.{}, {} SMs, fp16={})",
                     i, info.name, info.total_memory_mb, info.free_memory_mb,
                     info.cc_major, info.cc_minor, info.sm_count, info.fp16_supported);
    }
    return count > 0;
#else
    (void)out;
    out.gpu_count = 0;
    return false;
#endif
}
