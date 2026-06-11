#include "../include/ConfigAdvisor.h"

#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

// --- helpers ---

static std::string iso8601()
{
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

static float maxModelPerSampleMb(const AppConfig &cfg)
{
    float maxMb = 0.0f;
    auto check = [&](int w, int h, int c) {
        float mb = static_cast<float>(w) * static_cast<float>(h) *
                   static_cast<float>(c) * 4.0f / (1024.0f * 1024.0f);
        if (mb > maxMb) maxMb = mb;
    };
    // Check static models config
    for (auto &[name, entry] : cfg.models) {
        check(entry.input_width, entry.input_height, entry.input_channels);
    }
    // Check dynamic engines config (persisted by /models/load, not loaded yet)
    for (auto &entry : cfg.dynamic_engines) {
        check(entry.input_width, entry.input_height, entry.input_channels);
    }
    // If completely empty, use a conservative default
    if (maxMb <= 0.0f) maxMb = 5.0f;
    return maxMb;
}

static void writeRecommendationsToFile(const std::string &configPath,
                                       const AppConfig &cfg)
{
    try
    {
        json j;
        std::ifstream f(configPath);
        if (f.good())
        {
            try {
                f >> j;
            } catch (...) {
                // Start fresh if parse fails
                j = json::object();
            }
        }

        const auto &rec = cfg.recommendations;
        json r;
        r["generated_at"] = rec.generated_at;
        r["scenario"]     = rec.scenario;
        {
            json sp;
            sp["cpu"]            = rec.system_profile.cpu;
            sp["ram_gb"]         = rec.system_profile.ram_gb;
            sp["gpu"]            = rec.system_profile.gpu;
            sp["per_sample_mb"]  = rec.system_profile.per_sample_mb;
            sp["gpu_count"]      = rec.system_profile.gpu_count;
            sp["has_gpu"]        = rec.system_profile.has_gpu;
            r["system_profile"]  = sp;
        }
        {
            json profiles = json::object();
            for (auto &[key, prof] : rec.profiles)
            {
                json pj;
                pj["label"]      = prof.label;
                pj["risk_level"] = prof.risk_level;
                pj["best_for"]   = prof.best_for;
                pj["reason"]     = prof.reason;
                {
                    json params;
                    params["server_threads"]       = prof.params.server_threads;
                    params["max_batch_size"]       = prof.params.max_batch_size;
                    params["max_delay_ms"]         = prof.params.max_delay_ms;
                    params["workspace_mb"]         = prof.params.workspace_mb;
                    params["fp16"]                 = prof.params.fp16;
                    params["rate_limit_req_per_sec"] = prof.params.rate_limit_req_per_sec;
                    params["rate_limit_burst"]     = prof.params.rate_limit_burst;
                    pj["params"] = params;
                }
                profiles[key] = pj;
            }
            r["profiles"] = profiles;
        }

        j["recommendations"] = r;

        std::ofstream of(configPath);
        of << j.dump(2) << std::endl;

        spdlog::info("ConfigAdvisor: recommendations written to {}", configPath);
    }
    catch (const std::exception &e)
    {
        spdlog::error("ConfigAdvisor: failed to write recommendations: {}", e.what());
    }
}

// --- public API ---

void ConfigAdvisor::analyze(AppConfig &cfg,
                            const HardwareProfile &hw,
                            const ModelFactory *modelFactory,
                            const std::string &configPath)
{
    // Fill system_profile
    auto &sp = cfg.recommendations.system_profile;
    {
        std::ostringstream oss;
        oss << hw.cpu_logical_cores << " logical cores, "
            << hw.cpu_physical_cores << " physical";
        sp.cpu = oss.str();
        sp.ram_gb = static_cast<int>(hw.total_ram_gb);
        sp.has_gpu = hw.hasGpu();
        sp.gpu_count = hw.gpu_count;

        if (!hw.gpus.empty())
        {
            auto &g = hw.gpus[0];
            std::ostringstream gs;
            gs << g.name << " (" << g.total_memory_mb << "MB total, "
               << g.free_memory_mb << "MB free, CC "
               << g.cc_major << "." << g.cc_minor << ")";
            sp.gpu = gs.str();
        }
        else
        {
            sp.gpu = "none";
        }
        sp.per_sample_mb = maxModelPerSampleMb(cfg);
    }

    cfg.recommendations.generated_at = iso8601();
    cfg.recommendations.valid = true;

    compute(hw, nullptr, cfg);

    // Log summary
    spdlog::info("ConfigAdvisor: scenario={}, stable(threads={},batch={},delay={}ms,ws={}MB), "
                 "aggressive(threads={},batch={},delay={}ms,ws={}MB)",
                 cfg.recommendations.scenario,
                 cfg.recommendations.profiles["stable"].params.server_threads,
                 cfg.recommendations.profiles["stable"].params.max_batch_size,
                 cfg.recommendations.profiles["stable"].params.max_delay_ms,
                 cfg.recommendations.profiles["stable"].params.workspace_mb,
                 cfg.recommendations.profiles["aggressive"].params.server_threads,
                 cfg.recommendations.profiles["aggressive"].params.max_batch_size,
                 cfg.recommendations.profiles["aggressive"].params.max_delay_ms,
                 cfg.recommendations.profiles["aggressive"].params.workspace_mb);

    // First-time startup: auto-apply stable profile if no recommendations existed before
    {
        json j;
        {
            std::ifstream f(configPath);
            if (f.good()) { try { f >> j; } catch (...) { j = json::object(); } }
        }
        bool wasFirstTime = !j.contains("recommendations");

        // Auto-apply stable profile on first-time startup AND persist to config.json
        if (wasFirstTime)
        {
            const auto &s = cfg.recommendations.profiles["stable"].params;
            cfg.server.threads             = s.server_threads;
            cfg.batching.enabled           = (s.max_batch_size > 1);
            cfg.batching.max_batch_size    = s.max_batch_size;
            cfg.batching.max_delay_ms      = s.max_delay_ms;
            cfg.server.rate_limit_req_per_sec = s.rate_limit_req_per_sec;
            cfg.server.rate_limit_burst    = s.rate_limit_burst;

            spdlog::info("ConfigAdvisor: first-time startup detected, auto-applying stable profile "
                         "(threads={}, batch={}, delay={}ms, rate={}/s)",
                         s.server_threads, s.max_batch_size, s.max_delay_ms,
                         s.rate_limit_req_per_sec);

            // Write stable values to config.json so they survive container restart
            j["server"]["threads"] = s.server_threads;
            j["server"]["rate_limit_req_per_sec"] = s.rate_limit_req_per_sec;
            j["server"]["rate_limit_burst"] = s.rate_limit_burst;
            j["batching"]["enabled"] = (s.max_batch_size > 1);
            j["batching"]["max_batch_size"] = s.max_batch_size;
            j["batching"]["max_delay_ms"] = s.max_delay_ms;
            {
                std::ofstream of(configPath);
                of << j.dump(2) << std::endl;
            }
            spdlog::info("ConfigAdvisor: stable profile written to config.json");
        }
    }
}

void ConfigAdvisor::compute(const HardwareProfile &hw,
                            const ModelFactory * /*mf*/,
                            AppConfig &cfg)
{
    // Determine scenario from config entries (not from loaded models —
    // models may not be loaded at startup anymore).
    bool hasCpu = false;
    bool hasGpu = false;
    for (auto &[name, entry] : cfg.models) {
        if (entry.type == "tensorrt") hasGpu = true;
        else hasCpu = true;
    }
    for (auto &entry : cfg.dynamic_engines) {
        if (entry.type == "tensorrt") hasGpu = true;
        else hasCpu = true;
    }

    std::string scenario;
    if (!hasGpu)           scenario = "CPU_ONLY";
    else if (!hasCpu)      scenario = "GPU_ONLY";
    else                   scenario = "MIXED";

    cfg.recommendations.scenario = scenario;

    // Gather GPU info for formulas
    size_t freeVram  = hw.gpus.empty() ? 0 : hw.gpus[0].free_memory_mb;
    bool   fp16Avail = hw.gpus.empty() ? false : hw.gpus[0].fp16_supported;

    cfg.recommendations.profiles["stable"]     = buildProfile("stable", hw, scenario, freeVram, fp16Avail);
    cfg.recommendations.profiles["aggressive"] = buildProfile("aggressive", hw, scenario, freeVram, fp16Avail);
}

RecommendationProfile ConfigAdvisor::buildProfile(
    const std::string &key,
    const HardwareProfile &hw,
    const std::string &scenario,
    size_t freeVramMb,
    bool fp16Avail)
{
    using namespace ConfigBounds;

    RecommendationProfile prof;
    bool isAggressive = (key == "aggressive");
    int cpu = hw.cpu_logical_cores;

    if (isAggressive)
    {
        prof.label      = "性能模式";
        prof.risk_level  = "中";
        prof.best_for   = "压测环境、离线批处理";
        prof.reason     = "最大化利用显存和CPU线程，追求极致吞吐";
    }
    else
    {
        prof.label      = "稳定模式";
        prof.risk_level  = "低";
        prof.best_for   = "生产环境、7×24小时服务";
        prof.reason     = "保留充足系统资源冗余，优先保证稳定性";
    }

    float perSampleMb = hw.max_per_sample_mb;
    if (perSampleMb <= 0.0f) perSampleMb = 5.0f; // safe fallback

    // --- threads ---
    int threads = 4;
    if (scenario == "CPU_ONLY")
        threads = isAggressive ? clamp(static_cast<int>(cpu * 1.0f), 4, MAX_THREADS)
                               : clamp(static_cast<int>(cpu * 0.7f), 2, MAX_THREADS);
    else if (scenario == "GPU_ONLY")
        threads = isAggressive ? clamp(static_cast<int>(cpu * 0.5f), 2, 12)
                               : clamp(static_cast<int>(cpu * 0.3f), 2, 8);
    else // MIXED
        threads = isAggressive ? clamp(static_cast<int>(cpu * 0.8f), 4, 16)
                               : clamp(static_cast<int>(cpu * 0.5f), 4, 16);

    // --- max_batch ---
    int maxBatch = MIN_BATCH_SIZE;
    if (scenario == "CPU_ONLY")
    {
        maxBatch = isAggressive ? 16 : 8;
    }
    else
    {
        float factor = isAggressive ? 0.70f : 0.50f;
        int lo = isAggressive ? 8 : 4;
        int hi = isAggressive ? MAX_BATCH_SIZE : 16;
        if (perSampleMb > 0.0f && freeVramMb > 0)
        {
            int batchByMem = static_cast<int>(std::round(
                static_cast<float>(freeVramMb) * factor / perSampleMb));
            maxBatch = clamp(batchByMem, lo, hi);
        }
        else
        {
            maxBatch = lo;
        }
    }

    // --- max_delay_ms ---
    int delayMs = isAggressive ? 5 : 20;

    // --- workspace_mb ---
    int workspaceMb = 0;
    if (scenario != "CPU_ONLY" && hw.hasGpu())
    {
        if (isAggressive)
            workspaceMb = clamp(static_cast<int>(freeVramMb * 0.15f), MIN_WORKSPACE_MB, 2048);
        else
            workspaceMb = clamp(static_cast<int>(freeVramMb * 0.08f), MIN_WORKSPACE_MB, 512);
    }

    // --- rate_limit ---
    int rateLimit = 0;
    int rateBurst = 0;
    if (!isAggressive)
    {
        rateLimit = 1000;
        rateBurst = 2000;
    }

    prof.params.server_threads       = threads;
    prof.params.max_batch_size       = maxBatch;
    prof.params.max_delay_ms         = delayMs;
    prof.params.workspace_mb         = workspaceMb;
    prof.params.fp16                 = fp16Avail;
    prof.params.rate_limit_req_per_sec = rateLimit;
    prof.params.rate_limit_burst     = rateBurst;

    return prof;
}
