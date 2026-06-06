#!/usr/bin/env python3
"""Benchmark two models (ONNX vs TRT) and generate comparison report."""

import json, os, sys, time, urllib.request, urllib.error
from concurrent.futures import ThreadPoolExecutor, as_completed

BASE_URL = "http://localhost:80"
REQUESTS = int(os.environ.get("N", "50"))
CONCURRENCY = int(os.environ.get("C", "4"))
WARMUP = 5

def bench_model(name, image_b64):
    payload = json.dumps({"image_data": image_b64, "model_name": name}).encode()
    latencies = []
    for _ in range(WARMUP):
        try:
            req = urllib.request.Request(f"{BASE_URL}/predict", data=payload,
                headers={"Content-Type": "application/json"})
            urllib.request.urlopen(req, timeout=60).read()
        except:
            pass

    t0 = time.perf_counter()
    errors = 0
    with ThreadPoolExecutor(max_workers=CONCURRENCY) as pool:
        def once():
            try:
                t1 = time.perf_counter()
                req = urllib.request.Request(f"{BASE_URL}/predict", data=payload,
                    headers={"Content-Type": "application/json"})
                urllib.request.urlopen(req, timeout=60).read()
                return (time.perf_counter() - t1) * 1_000_000, True
            except:
                return 0, False
        futures = [pool.submit(once) for _ in range(REQUESTS)]
        for f in as_completed(futures):
            lat, ok = f.result()
            if ok:
                latencies.append(lat)
            else:
                errors += 1
    total_t = time.perf_counter() - t0
    latencies.sort()
    n = len(latencies)
    if n == 0:
        return {"qps": 0, "p50": 0, "p95": 0, "p99": 0, "avg": 0, "errors": REQUESTS, "time": total_t}
    return {
        "qps": REQUESTS / total_t,
        "avg": sum(latencies) / n,
        "p50": latencies[n // 2],
        "p95": latencies[int(n * 0.95)],
        "p99": latencies[int(n * 0.99)],
        "min": latencies[0],
        "max": latencies[-1],
        "errors": errors,
        "time": total_t,
    }

def fetch_model_metrics(model_key):
    try:
        with urllib.request.urlopen(f"{BASE_URL}/metrics/json") as resp:
            mi = json.loads(resp.read()).get("model_inference", {})
            return mi.get(model_key, {})
    except:
        return {}

def main():
    print("Loading test image...")
    import base64
    with open("/mnt/d/jetbrains/clion-project/httpserver/images/cat.jpg", "rb") as f:
        img_b64 = base64.b64encode(f.read()).decode()

    models = [
        ("squeezenet1.1-7_onnx:1", "ONNX", "classification"),
        ("resnet50_trt:1", "TensorRT FP16", "classification"),
    ]

    results = {}
    for name, backend, task in models:
        print(f"\nBenchmarking {name} ({backend})...")
        key = f"{name.split(':')[0]}:{task}"
        results[key] = bench_model(name, img_b64)
        r = results[key]
        print(f"  QPS={r['qps']:.1f}, avg={r['avg']:.0f}us, P50={r['p50']:.0f}us, "
              f"P95={r['p95']:.0f}us, P99={r['p99']:.0f}us, errors={r['errors']}")

    # Fetch model-level metrics from server
    server_metrics = {}
    with urllib.request.urlopen(f"{BASE_URL}/metrics/json") as resp:
        sm = json.loads(resp.read()).get("model_inference", {})
        for key, mm in sm.items():
            server_metrics[key] = {
                "count": mm["count"],
                "avg_us": mm["avg_latency_us"],
                "min_us": mm["latency_us_min"],
                "max_us": mm["latency_us_max"],
            }

    # Write report
    report = f"pressureresult-comparison-{time.strftime('%Y%m%d-%H%M%S')}.md"
    with open(report, "w") as f:
        f.write("# Inference Benchmark: ONNX vs TensorRT\n\n")
        f.write(f"**Date:** {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"**Requests per model:** {REQUESTS}, **concurrency:** {CONCURRENCY}\n\n")
        f.write("## Results\n\n")
        f.write("| Model | Backend | QPS | Avg (us) | P50 (us) | P95 (us) | P99 (us) | Errors |\n")
        f.write("|-------|---------|-----|----------|----------|----------|----------|--------|\n")
        for (name, backend, task), (key, r) in zip(models, results.items()):
            f.write(f"| {name.split(':')[0]} | {backend} | {r['qps']:.1f} | {r['avg']:.0f} | "
                    f"{r['p50']:.0f} | {r['p95']:.0f} | {r['p99']:.0f} | {r['errors']} |\n")

        f.write("\n## Server Model Metrics (lifetime)\n\n")
        f.write("| Model:Task | Count | Avg (us) | Min (us) | Max (us) |\n")
        f.write("|------------|-------|----------|----------|----------|\n")
        for key, mm in server_metrics.items():
            f.write(f"| {key} | {mm['count']} | {mm['avg_us']} | {mm['min_us']} | {mm['max_us']} |\n")

        f.write("\n## Analysis\n\n")
        s_r = results.get("squeezenet1.1-7_onnx:classification", {})
        r_r = results.get("resnet50_trt:classification", {})
        if s_r.get("qps", 0) > 0 and r_r.get("qps", 0) > 0:
            ratio = r_r["qps"] / s_r["qps"]
            f.write(f"- TRT QPS / ONNX QPS ratio: **{ratio:.2f}x**\n")
        if s_r.get("p50", 0) > 0 and r_r.get("p50", 0) > 0:
            latency_ratio = s_r["p50"] / r_r["p50"]
            f.write(f"- ONNX P50 / TRT P50 latency ratio: **{latency_ratio:.2f}x**\n")

    print(f"\nReport saved: {report}")
    with open(report) as f:
        print(f.read())

if __name__ == "__main__":
    main()
