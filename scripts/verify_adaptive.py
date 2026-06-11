import json, urllib.request

# Fetch hardware endpoint
req = urllib.request.Request('http://localhost:80/system/hardware')
d = json.loads(urllib.request.urlopen(req).read().decode())
sp = d['system_profile']
cur = d['current']

print('========== 自适应设备检测 验证报告 ==========')
print()

# 1. Hardware detection
print('[1] 硬件探测')
print(f"    CPU: {sp['cpu']}")
print(f"    RAM: {sp['ram_gb']} GB")
print(f"    GPU: {sp['gpu']}")
print(f"    GPU Count: {sp['gpu_count']}")
print(f"    Scenario: {d['scenario']}")
print(f"    Valid: {d['valid']}")

assert sp['ram_gb'] == 15
assert sp['gpu_count'] == 1
assert sp['has_gpu'] == True
assert d['scenario'] == 'MIXED'
print('[1] PASSED')

# 2. Stable profile
print('\n[2] 稳定版推荐')
sta = d['profiles']['stable']['params']
print(f"    threads={sta['server_threads']}, batch={sta['max_batch_size']}, delay={sta['max_delay_ms']}ms, ws={sta['workspace_mb']}MB, fp16={sta['fp16']}, rate={sta['rate_limit_req_per_sec']}/s")
assert sta['server_threads'] == 8, f"threads={sta['server_threads']}"
assert sta['max_batch_size'] == 16, f"batch={sta['max_batch_size']}"
assert sta['max_delay_ms'] == 20
assert sta['workspace_mb'] == 512, f"ws={sta['workspace_mb']}"
assert sta['fp16'] == True
assert sta['rate_limit_req_per_sec'] == 1000
print('[2] PASSED - All stable formulas correct')

# 3. Aggressive profile
print('\n[3] 性能版推荐')
agg = d['profiles']['aggressive']['params']
print(f"    threads={agg['server_threads']}, batch={agg['max_batch_size']}, delay={agg['max_delay_ms']}ms, ws={agg['workspace_mb']}MB, fp16={agg['fp16']}, rate={agg['rate_limit_req_per_sec']}/s")
assert agg['server_threads'] == 12, f"threads={agg['server_threads']}"
assert agg['max_batch_size'] == 64, f"batch={agg['max_batch_size']}"
assert agg['max_delay_ms'] == 5
assert agg['workspace_mb'] == 978, f"ws={agg['workspace_mb']}"
assert agg['fp16'] == True
assert agg['rate_limit_req_per_sec'] == 0
print('[3] PASSED - All aggressive formulas correct')

# 4. Current matches stable
print('\n[4] 当前配置匹配')
match_sta = all([
    cur['server_threads'] == sta['server_threads'],
    cur['max_batch_size'] == sta['max_batch_size'],
    cur['max_delay_ms'] == sta['max_delay_ms'],
    cur['rate_limit_req_per_sec'] == sta['rate_limit_req_per_sec'],
])
print(f"    threads={cur['server_threads']}, batch={cur['max_batch_size']}, delay={cur['max_delay_ms']}ms, rate={cur['rate_limit_req_per_sec']}/s")
print(f"    匹配稳定版: {'YES' if match_sta else 'NO'}")
assert match_sta
print('[4] PASSED')

print('\n========== 全部通过 ==========')
