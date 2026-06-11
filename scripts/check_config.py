import json, sys
d = json.load(open(sys.argv[1]))
s = d.get("server", {})
b = d.get("batching", {})
rec = d.get("recommendations", {})
print("=== 运行时配置 ===")
print(f"threads={s.get('threads')}, rate_limit={s.get('rate_limit_req_per_sec')}/s, burst={s.get('rate_limit_burst')}")
print(f"batch_enabled={b.get('enabled')}, max_batch={b.get('max_batch_size')}, delay={b.get('max_delay_ms')}ms")
if rec:
    print(f"scenario={rec.get('scenario')}")
    sp = rec.get("system_profile", {})
    print(f"detected: cpu={sp.get('cpu')}, ram={sp.get('ram_gb')}GB, gpu={sp.get('gpu')}")
    print(f"per_sample_mb={sp.get('per_sample_mb')}")
    for k in ["stable", "aggressive"]:
        p = rec.get("profiles", {}).get(k, {}).get("params", {})
        if p:
            print(f"  {k}: threads={p.get('server_threads')}, batch={p.get('max_batch_size')}, delay={p.get('max_delay_ms')}ms, ws={p.get('workspace_mb')}MB, fp16={p.get('fp16')}, rate={p.get('rate_limit_req_per_sec')}/s")
else:
    print("recommendations: NOT PRESENT")
print()
print("=== 推荐 vs 当前对比 ===")
for k in ["stable", "aggressive"]:
    p = rec.get("profiles", {}).get(k, {}).get("params", {})
    if not p: continue
    match_threads = s.get("threads") == p.get("server_threads")
    match_batch = b.get("max_batch_size") == p.get("max_batch_size")
    match_delay = b.get("max_delay_ms") == p.get("max_delay_ms")
    match_rate = s.get("rate_limit_req_per_sec") == p.get("rate_limit_req_per_sec")
    all_match = match_threads and match_batch and match_delay and match_rate
    print(f"  {k}: {'✅ 已应用' if all_match else '❌ 未应用'}")
