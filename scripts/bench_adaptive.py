#!/usr/bin/env python3
"""3-phase stress test: RAMP → HOLD → COMPARE, 8 models × 2 profiles.

Uses /predict/raw (binary JPEG body) for maximum throughput — no JSON parse,
no base64 decode. Falls back to /predict (JSON) for ONNX if needed.

Usage:
  python3 scripts/bench_adaptive.py --ramp       # Phase 1 only (fast, ~5 min)
  python3 scripts/bench_adaptive.py --hold       # Phase 2 only
  python3 scripts/bench_adaptive.py --compare    # Phase 3 only
  python3 scripts/bench_adaptive.py --all        # All 3 phases (full, ~25 min)
"""

import argparse, base64, http.cookiejar, json, os, statistics, sys, time, urllib.request, urllib.error
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

# ── Config ──────────────────────────────────────────────────────────
BASE_URL  = "http://localhost:80"
USERNAME  = "user1"
PASSWORD  = "123456"
IMAGE_DIR = Path(__file__).resolve().parent.parent / "images"
RAMP_CONCURRENCIES = [1, 2, 4, 8, 16, 32, 64, 128, 256]
RAMP_DURATION_SEC   = 30     # seconds per concurrency level
HOLD_DURATION_SEC   = 120    # seconds for sustained load
COMPARE_REQUESTS    = 200    # requests per model in compare phase
COMPARE_CONCURRENCY = 8      # concurrency for compare phase

# 4 pairs × 2 backends = 8 models
MODELS = [
    {"name": "resnet50_classification_trt",  "task": "classification"},
    {"name": "resnet50_classification_onnx", "task": "classification"},
    {"name": "squeezenet1.1-7_trt",         "task": "classification"},
    {"name": "squeezenet1.1-7_onnx",        "task": "classification"},
    {"name": "vision_model_trt",             "task": "feature_extraction"},
    {"name": "vision_model_onnx",            "task": "feature_extraction"},
    {"name": "yolov8l_trt",                  "task": "detection"},
    {"name": "yolov8l_onnx",                 "task": "detection"},
]

# ── Helpers ─────────────────────────────────────────────────────────

def load_images_raw():
    """Load all test images as raw JPEG/PNG bytes (for /predict/raw)."""
    images = []
    for ext in ("*.jpg", "*.jpeg", "*.png"):
        for p in sorted(IMAGE_DIR.glob(ext)):
            with open(p, "rb") as f:
                images.append(f.read())
    if not images:
        sys.exit("ERROR: No images found in " + str(IMAGE_DIR))
    return images

def load_images_b64():
    """Load and base64-encode all test images (for /predict JSON fallback)."""
    images = []
    for ext in ("*.jpg", "*.jpeg", "*.png"):
        for p in sorted(IMAGE_DIR.glob(ext)):
            with open(p, "rb") as f:
                images.append(base64.b64encode(f.read()).decode())
    if not images:
        sys.exit("ERROR: No images found in " + str(IMAGE_DIR))
    return images


def login():
    """Return an opener with session cookie."""
    cj = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
    req = urllib.request.Request(
        f"{BASE_URL}/login",
        data=json.dumps({"username": USERNAME, "password": PASSWORD}).encode(),
        headers={"Content-Type": "application/json"})
    resp = json.loads(opener.open(req, timeout=10).read().decode())
    if resp.get("status") != "ok":
        sys.exit(f"Login failed: {resp}")
    return opener


def load_all_models(opener):
    """Load all 8 models via /models/load. Respects 'already loaded' 409."""
    models_config = [
        {"name": "resnet50_classification_trt",  "type": "tensorrt", "path": "models/resnet50_classification.engine", "task": "classification"},
        {"name": "resnet50_classification_onnx", "type": "onnx",     "path": "models/resnet50_classification.onnx",   "task": "classification"},
        {"name": "squeezenet1.1-7_trt",         "type": "tensorrt", "path": "models/squeezenet1.1-7.engine",         "task": "classification"},
        {"name": "squeezenet1.1-7_onnx",        "type": "onnx",     "path": "models/squeezenet1.1-7.onnx",           "task": "classification"},
        {"name": "vision_model_trt",             "type": "tensorrt", "path": "models/vision_model.engine",            "task": "feature_extraction"},
        {"name": "vision_model_onnx",            "type": "onnx",     "path": "models/vision_model.onnx",              "task": "feature_extraction"},
        {"name": "yolov8l_trt",                  "type": "tensorrt", "path": "models/yolov8l.engine",                 "task": "detection"},
        {"name": "yolov8l_onnx",                 "type": "onnx",     "path": "models/yolov8l.onnx",                   "task": "detection"},
    ]
    for m in models_config:
        body = json.dumps({"name": m["name"], "version": "1", "type": m["type"],
                           "path": m["path"], "task": m["task"]}).encode()
        try:
            r = json.loads(opener.open(urllib.request.Request(
                f"{BASE_URL}/models/load", data=body,
                headers={"Content-Type": "application/json"}), timeout=30).read().decode())
            status = r.get("status", "?")
            msg = r.get("message", "")[:60]
            print(f"  Load {m['name']}: {status} {msg}")
        except Exception as e:
            print(f"  Load {m['name']}: ERROR {e}")


def apply_profile(opener, profile_key):
    """Apply a config profile, restart, wait for health, then reload all models."""
    resp = json.loads(opener.open(urllib.request.Request(
        f"{BASE_URL}/system/config/apply",
        data=json.dumps({"profile": profile_key}).encode(),
        headers={"Content-Type": "application/json"}), timeout=10).read().decode())
    assert resp["status"] == "ok", f"Apply {profile_key} failed: {resp}"

    # Restart
    print(f"  Restarting to apply {profile_key}...")
    try:
        opener.open(urllib.request.Request(
            f"{BASE_URL}/system/restart", data=b"",
            headers={"Content-Type": "application/json"}), timeout=3)
    except Exception:
        pass  # connection drop expected

    # Wait for health
    for i in range(60):
        try:
            r = urllib.request.urlopen(f"{BASE_URL}/health", timeout=3)
            if json.loads(r.read().decode()).get("status") == "ok":
                break
        except Exception:
            pass
        time.sleep(1)
    else:
        sys.exit("Server did not come back after restart")

    # Give the server a moment to fully initialize
    time.sleep(2)

    # Re-login (session was lost on restart)
    opener = login()
    time.sleep(1)

    # Reload all models (restart drops them)
    print(f"  Reloading models...")
    load_all_models(opener)

    return opener


def verify_profile(expected_profile) -> dict:
    """Check /system/hardware current matches expected profile params."""
    hw = json.loads(urllib.request.urlopen(
        f"{BASE_URL}/system/hardware", timeout=10).read().decode())
    cur = hw["current"]
    target = hw["profiles"][expected_profile]["params"]
    for k in ["server_threads", "max_batch_size", "max_delay_ms", "rate_limit_req_per_sec"]:
        if cur[k] != target[k]:
            print(f"  WARNING: current.{k}={cur[k]} != {expected_profile}.{k}={target[k]}")
    return hw["profiles"]


def predict_once_raw(opener, model_name, img_raw):
    """Send one prediction via /predict/raw (binary JPEG body).
       Returns (latency_us, ok, status_code)."""
    req = urllib.request.Request(
        f"{BASE_URL}/predict/raw?model_name={model_name}",
        data=img_raw,
        headers={"Content-Type": "image/jpeg"})
    t0 = time.perf_counter()
    try:
        resp = urllib.request.urlopen(req, timeout=30)
        body = resp.read()
        elapsed = (time.perf_counter() - t0) * 1_000_000
        data = json.loads(body) if body else {}
        return elapsed, data.get("status") == "ok", resp.status
    except urllib.error.HTTPError as e:
        return (time.perf_counter() - t0) * 1_000_000, False, e.code
    except Exception as e:
        return (time.perf_counter() - t0) * 1_000_000, False, 0


# ── Phase 1: RAMP ──────────────────────────────────────────────────

def run_ramp(opener, model_name, images_raw, label):
    """RAMP phase: increment concurrency, find saturation point."""
    print(f"\n{'='*60}")
    print(f"  RAMP: {label}")
    print(f"  Model: {model_name} | Duration: {RAMP_DURATION_SEC}s/level")
    print(f"  Path: /predict/raw (binary JPEG)")
    print(f"{'='*60}")
    print(f"{'Concurrency':>12} | {'QPS':>8} | {'P50(ms)':>9} | {'P95(ms)':>9} | {'P99(ms)':>9} | {'Err':>5}")
    print("-" * 75)

    results = []
    for c in RAMP_CONCURRENCIES:
        # Warmup
        for _ in range(5):
            predict_once_raw(opener, model_name, images_raw[_ % len(images_raw)])

        latencies = []
        errors = 0
        t_start = time.perf_counter()

        def worker():
            img = images_raw[int(time.time() * 1000) % len(images_raw)]
            lat, ok, _ = predict_once_raw(opener, model_name, img)
            return lat, ok

        with ThreadPoolExecutor(max_workers=c) as pool:
            end_time = time.perf_counter() + RAMP_DURATION_SEC
            futures = []
            while time.perf_counter() < end_time:
                while len(futures) < c * 2:
                    futures.append(pool.submit(worker))
                done = []
                for f in futures:
                    if f.done():
                        done.append(f)
                for f in done:
                    futures.remove(f)
                    try:
                        lat, ok = f.result()
                        if ok:
                            latencies.append(lat)
                        else:
                            errors += 1
                    except Exception:
                        errors += 1
                time.sleep(0.001)

        t_total = time.perf_counter() - t_start
        n = len(latencies)
        if n == 0:
            print(f"  {c:>10} | {'—':>8} | {'—':>9} | {'—':>9} | {'—':>9} | {errors:>5}")
            results.append({"concurrency": c, "qps": 0, "errors": errors, "latencies": []})
            continue

        latencies.sort()
        qps = n / t_total if t_total > 0 else 0
        p50 = latencies[n // 2] / 1000
        p95 = latencies[int(n * 0.95)] / 1000 if n > 1 else latencies[0] / 1000
        p99 = latencies[int(n * 0.99)] / 1000 if n > 2 else latencies[-1] / 1000

        print(f"  {c:>10} | {qps:>8.1f} | {p50:>8.1f}ms | {p95:>8.1f}ms | {p99:>8.1f}ms | {errors:>5}")

        results.append({
            "concurrency": c,
            "qps": round(qps, 1),
            "p50_ms": round(p50, 1),
            "p95_ms": round(p95, 1),
            "p99_ms": round(p99, 1),
            "errors": errors,
            "n": n,
        })

        # Stop early if saturation detected
        if len(results) >= 3 and c >= 16:
            prev = results[-2]["qps"]
            prev2 = results[-3]["qps"]
            if prev > 0 and prev2 > 0:
                delta = abs(results[-1]["qps"] - prev) / prev
                if delta < 0.03:
                    print(f"  → Saturated at concurrency={c}")
                    break

    return results


# ── Phase 2: HOLD ──────────────────────────────────────────────────

def run_hold(opener, model_name, images_raw, concurrency, label):
    """HOLD phase: sustained load at target concurrency."""
    print(f"\n{'='*60}")
    print(f"  HOLD: {label} @ concurrency={concurrency} for {HOLD_DURATION_SEC}s")
    print(f"  Model: {model_name}  |  Path: /predict/raw")
    print(f"{'='*60}")

    # Warmup
    for _ in range(10):
        predict_once_raw(opener, model_name, images_raw[_ % len(images_raw)])

    samples = []
    errors = 0
    t_start = time.perf_counter()

    def worker():
        img = images_raw[int(time.time() * 1000) % len(images_raw)]
        lat, ok, _ = predict_once_raw(opener, model_name, img)
        return time.perf_counter(), lat, ok

    with ThreadPoolExecutor(max_workers=concurrency) as pool:
        end_time = time.perf_counter() + HOLD_DURATION_SEC
        futures = []
        while time.perf_counter() < end_time:
            while len(futures) < concurrency * 2:
                futures.append(pool.submit(worker))
            done = []
            for f in futures:
                if f.done():
                    done.append(f)
            for f in done:
                futures.remove(f)
                try:
                    ts, lat, ok = f.result()
                    if ok:
                        samples.append((ts, lat))
                    else:
                        errors += 1
                except Exception:
                    errors += 1
            time.sleep(0.001)

    if not samples:
        return {"qps_avg": 0, "qps_std": 0, "errors": errors}

    n = len(samples)

    # QPS in 10-second windows
    windows = []
    window_size = 10.0
    t0 = samples[0][0]
    while t0 + window_size <= samples[-1][0]:
        count = sum(1 for ts, _ in samples if t0 <= ts < t0 + window_size)
        windows.append(count / window_size)
        t0 += window_size

    times = [lat / 1000 for _, lat in samples]
    times.sort()

    qps_avg = round(sum(windows) / len(windows), 1) if windows else 0
    qps_std = round(statistics.stdev(windows), 1) if len(windows) > 1 else 0

    # P99 drift: compare first 10s vs last 10s
    p99_first = [lat for ts, lat in samples if ts < samples[0][0] + 10]
    p99_last  = [lat for ts, lat in samples if ts > samples[-1][0] - 10]
    p99_first.sort()
    p99_last.sort()
    p99_drift_pct = 0
    if p99_first and p99_last:
        p99_f = p99_first[int(len(p99_first) * 0.99)] if len(p99_first) > 2 else p99_first[-1]
        p99_l = p99_last[int(len(p99_last) * 0.99)] if len(p99_last) > 2 else p99_last[-1]
        p99_drift_pct = round((p99_l - p99_f) / max(1, p99_f) * 100, 1)

    p50 = times[n // 2]
    p95 = times[int(n * 0.95)] if n > 1 else times[0]
    p99 = times[int(n * 0.99)] if n > 2 else times[-1]

    print(f"  Avg QPS: {qps_avg}  StdDev: {qps_std}  P99 Drift: {p99_drift_pct}%")
    print(f"  P50={p50:.1f}ms  P95={p95:.1f}ms  P99={p99:.1f}ms  Errors: {errors}")
    return {
        "qps_avg": qps_avg, "qps_std": qps_std,
        "p50_ms": round(p50, 1), "p95_ms": round(p95, 1), "p99_ms": round(p99, 1),
        "p99_drift_pct": p99_drift_pct, "errors": errors
    }


# ── Phase 3: COMPARE ───────────────────────────────────────────────

def run_compare(opener, images_raw, label):
    """COMPARE phase: benchmark all 8 models sequentially via /predict/raw."""
    print(f"\n{'='*60}")
    print(f"  COMPARE: {label} | {COMPARE_REQUESTS} req/model, concurrency={COMPARE_CONCURRENCY}")
    print(f"  Path: /predict/raw (binary JPEG)")
    print(f"{'='*60}")
    header = f"{'Model':>32s} | {'Type':>7} | {'QPS':>8} | {'P50(ms)':>9} | {'P95(ms)':>9} | {'P99(ms)':>9} | {'Err':>5}"
    print(header)
    print("-" * len(header))

    results = {}
    for m in MODELS:
        model_name = m["name"] + ":1"
        # Warmup
        for _ in range(5):
            predict_once_raw(opener, model_name, images_raw[_ % len(images_raw)])

        latencies = []
        errors = 0

        def worker():
            img = images_raw[int(time.time() * 1000) % len(images_raw)]
            return predict_once_raw(opener, model_name, img)

        t_start = time.perf_counter()
        with ThreadPoolExecutor(max_workers=COMPARE_CONCURRENCY) as pool:
            futures = [pool.submit(worker) for _ in range(COMPARE_REQUESTS)]
            for f in as_completed(futures):
                try:
                    lat, ok, _ = f.result()
                    if ok:
                        latencies.append(lat)
                    else:
                        errors += 1
                except Exception:
                    errors += 1
        t_total = time.perf_counter() - t_start

        n = len(latencies)
        if n == 0:
            print(f"  {model_name:>30s} | {'—':>7} | {'—':>8} | {'—':>9} | {'—':>9} | {'—':>9} | {errors:>5}")
            results[m["name"]] = {"qps": 0, "errors": errors}
            continue

        latencies.sort()
        qps = COMPARE_REQUESTS / t_total
        p50 = latencies[n // 2] / 1000
        p95 = latencies[int(n * 0.95)] / 1000 if n > 1 else latencies[0] / 1000
        p99 = latencies[int(n * 0.99)] / 1000 if n > 2 else latencies[-1] / 1000

        print(f"  {model_name:>30s} | {'TRT' if '_trt' in m['name'] else 'ONNX':>7} | {qps:>8.1f} | {p50:>8.1f}ms | {p95:>8.1f}ms | {p99:>8.1f}ms | {errors:>5}")

        results[m["name"]] = {
            "qps": round(qps, 1),
            "p50_ms": round(p50, 1),
            "p95_ms": round(p95, 1),
            "p99_ms": round(p99, 1),
            "errors": errors,
            "type": "tensorrt" if "_trt" in m["name"] else "onnx",
            "model": m["name"].replace("_trt", "").replace("_onnx", ""),
        }

    return results


def print_final_report(ramp_stable, ramp_aggr, hold_stable, hold_aggr, comp_stable, comp_aggr):
    """Generate final comparison report."""
    print("\n\n" + "=" * 75)
    print("  ========== 自适应配置极限压力测试报告 ==========")
    print("  /predict/raw  (binary JPEG, Phase 4+5+6 optimized)")
    print("=" * 75)

    # Phase 1: RAMP table
    if ramp_stable and ramp_aggr:
        print(f"\n  ── [1] 并发饱和曲线 ── (squeezenet1.1-7 TRT, {RAMP_DURATION_SEC}s/level)")
        print(f"  {'Concurrency':>12} | {'Stable QPS':>10} | {'Stable P99':>10} | {'Aggr QPS':>10} | {'Aggr P99':>10}")
        print(f"  {'-'*12}-+-{'-'*10}-+-{'-'*10}-+-{'-'*10}-+-{'-'*10}")
        for i in range(max(len(ramp_stable), len(ramp_aggr))):
            rs = ramp_stable[i] if i < len(ramp_stable) else None
            ra = ramp_aggr[i] if i < len(ramp_aggr) else None
            c = rs["concurrency"] if rs else ra["concurrency"]
            sqps = f"{rs['qps']}" if rs else "—"
            sp99 = f"{rs['p99_ms']}ms" if rs else "—"
            aqps = f"{ra['qps']}" if ra else "—"
            ap99 = f"{ra['p99_ms']}ms" if ra else "—"
            marker = ""
            if rs and ra:
                delta = ra["qps"] - rs["qps"]
                if abs(delta) / max(1, rs["qps"]) > 0.1:
                    marker = " ★" if delta > 0 else " ▼"
            print(f"  {c:>12} | {sqps:>10} | {sp99:>10} | {aqps:>10} | {ap99:>10}{marker}")

    # Phase 2: HOLD table
    if hold_stable and hold_aggr:
        print(f"\n  ── [2] 持续负载稳定性 ── ({HOLD_DURATION_SEC}s)")
        print(f"  {'Metric':>15} | {'Stable':>12} | {'Aggressive':>12}")
        print(f"  {'-'*15}-+-{'-'*12}-+-{'-'*12}")
        print(f"  {'Avg QPS':>15} | {str(hold_stable.get('qps_avg','—')):>12} | {str(hold_aggr.get('qps_avg','—')):>12}")
        print(f"  {'QPS StdDev':>15} | {str(hold_stable.get('qps_std','—')):>12} | {str(hold_aggr.get('qps_std','—')):>12}")
        print(f"  {'P50':>15} | {str(hold_stable.get('p50_ms','—')+'ms'):>12} | {str(hold_aggr.get('p50_ms','—')+'ms'):>12}")
        print(f"  {'P99':>15} | {str(hold_stable.get('p99_ms','—')+'ms'):>12} | {str(hold_aggr.get('p99_ms','—')+'ms'):>12}")
        print(f"  {'P99 Drift %':>15} | {str(hold_stable.get('p99_drift_pct','—')):>12} | {str(hold_aggr.get('p99_drift_pct','—')):>12}")
        print(f"  {'Errors':>15} | {str(hold_stable.get('errors','—')):>12} | {str(hold_aggr.get('errors','—')):>12}")

    # Phase 3: COMPARE table
    if comp_stable and comp_aggr:
        print(f"\n  ── [3] 逐模型对比 ── ({COMPARE_REQUESTS} req/model, conc={COMPARE_CONCURRENCY})")
        print(f"  {'Model':>28s} | {'Type':>7} | {'S QPS':>7} | {'A QPS':>7} | {'Δ%':>6} | {'S P50':>7} | {'A P50':>7} | {'TRT/ONNX':>9}")
        print(f"  {'-'*28}-+-{'-'*7}-+-{'-'*7}-+-{'-'*7}-+-{'-'*6}-+-{'-'*7}-+-{'-'*7}-+-{'-'*9}")

        trt_speedups_stable = []
        trt_speedups_aggr = []
        aggr_gains = []

        for key in [m["name"] for m in MODELS]:
            s = comp_stable.get(key, {})
            a = comp_aggr.get(key, {})
            backend_type = "TRT" if "_trt" in key else "ONNX"

            sqps = s.get("qps", 0)
            aqps = a.get("qps", 0)
            sp50 = f"{s.get('p50_ms', 0)}ms"
            ap50 = f"{a.get('p50_ms', 0)}ms"

            delta_pct = round((aqps - sqps) / max(1, sqps) * 100, 1) if sqps > 0 else 0

            # ONNX→TRT speedup
            speedup = ""
            if "_onnx" in key:
                trt_key = key.replace("_onnx", "_trt")
                trt_s = comp_stable.get(trt_key, {})
                trt_a = comp_aggr.get(trt_key, {})
                if sqps > 0 and trt_s.get("qps", 0) > 0:
                    su = trt_s["qps"] / sqps
                    speedup = f"{su:.1f}x"
                    trt_speedups_stable.append(su)
                if aqps > 0 and trt_a.get("qps", 0) > 0:
                    trt_speedups_aggr.append(trt_a["qps"] / aqps)

            if sqps > 0 and aqps > 0:
                aggr_gains.append(delta_pct)

            print(f"  {key:>28s} | {backend_type:>7} | {sqps:>7} | {aqps:>7} | {delta_pct:>+5}% | {sp50:>7} | {ap50:>7} | {speedup:>9}")

        if aggr_gains:
            print(f"\n  {'─'*75}")
            print(f"  性能版 vs 稳定版 QPS 平均提升: {sum(aggr_gains)/len(aggr_gains):+.1f}%")
        if trt_speedups_stable:
            print(f"  稳定版 TRT/ONNX 平均加速比: {sum(trt_speedups_stable)/len(trt_speedups_stable):.1f}x")
        if trt_speedups_aggr:
            print(f"  性能版 TRT/ONNX 平均加速比: {sum(trt_speedups_aggr)/len(trt_speedups_aggr):.1f}x")

    # Summary
    print(f"\n  ── 测试配置 ──")
    print(f"  Endpoint: /predict/raw (binary JPEG body)")
    print(f"  Hardware: RTX 5060 (8GB VRAM), 16-core, 15GB RAM")
    print(f"  RAMP: {RAMP_DURATION_SEC}s/level, HOLD: {HOLD_DURATION_SEC}s, COMPARE: {COMPARE_REQUESTS}req/model @ conc={COMPARE_CONCURRENCY}")
    print(f"  Profiles: stable (threads=8 batch=16 delay=20ms) vs aggressive (threads=12 batch=64 delay=5ms)")


# ── Main ────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Adaptive config stress test")
    parser.add_argument("--ramp", action="store_true", help="Phase 1: concurrency ramp")
    parser.add_argument("--hold", action="store_true", help="Phase 2: sustained load")
    parser.add_argument("--compare", action="store_true", help="Phase 3: model comparison")
    parser.add_argument("--all", action="store_true", help="All 3 phases (default)")
    parser.add_argument("--quick", action="store_true", help="Quick mode: 10s ramp, 30s hold, 50 req compare")
    args = parser.parse_args()

    global RAMP_DURATION_SEC, HOLD_DURATION_SEC, COMPARE_REQUESTS
    if args.quick:
        RAMP_DURATION_SEC = 10
        HOLD_DURATION_SEC = 30
        COMPARE_REQUESTS = 50

    if not (args.ramp or args.hold or args.compare):
        args.all = True

    images_raw = load_images_raw()
    print(f"Loaded {len(images_raw)} raw images from {IMAGE_DIR}")
    print(f"Target: {BASE_URL}")

    # Health check
    try:
        r = urllib.request.urlopen(f"{BASE_URL}/health", timeout=5)
        if json.loads(r.read().decode()).get("status") != "ok":
            sys.exit("Health check failed")
        print("Health: OK")
    except Exception as e:
        sys.exit(f"Cannot reach {BASE_URL}/health: {e}")

    # Verify models loaded — load them if needed
    opener = login()
    print("Login: OK")

    models_resp = json.loads(urllib.request.urlopen(f"{BASE_URL}/models", timeout=10).read().decode())
    loaded_names = {m["name"] for m in models_resp}
    missing = [m["name"] for m in MODELS if m["name"] not in loaded_names]
    if missing:
        print(f"  Loading {len(missing)} missing models...")
        load_all_models(opener)

    # Model for RAMP/HOLD: fastest = squeezenet TRT
    FAST_MODEL = "squeezenet1.1-7_trt:1"

    ramp_stable = ramp_aggr = None
    hold_stable = hold_aggr = None
    comp_stable = comp_aggr = None

    # ── Stable Profile ────────────────────────────────────────────
    print("\n\n>>> Applying Stable Profile <<<")
    opener = apply_profile(opener, "stable")
    verify_profile("stable")
    print("Stable profile: verified")

    if args.all or args.ramp:
        ramp_stable = run_ramp(opener, FAST_MODEL, images_raw, "Stable")

    if args.all or args.hold:
        sat_conc = 32
        if ramp_stable:
            sat_conc = max(ramp_stable, key=lambda r: r["qps"]).get("concurrency", 32)
        hold_stable = run_hold(opener, FAST_MODEL, images_raw, sat_conc, "Stable")

    if args.all or args.compare:
        comp_stable = run_compare(opener, images_raw, "Stable")

    # ── Aggressive Profile ────────────────────────────────────────
    print("\n\n>>> Applying Aggressive Profile <<<")
    opener = apply_profile(opener, "aggressive")
    verify_profile("aggressive")
    print("Aggressive profile: verified")

    if args.all or args.ramp:
        ramp_aggr = run_ramp(opener, FAST_MODEL, images_raw, "Aggressive")

    if args.all or args.hold:
        sat_conc_a = 32
        if ramp_aggr:
            sat_conc_a = max(ramp_aggr, key=lambda r: r["qps"]).get("concurrency", 32)
        hold_aggr = run_hold(opener, FAST_MODEL, images_raw, sat_conc_a, "Aggressive")

    if args.all or args.compare:
        comp_aggr = run_compare(opener, images_raw, "Aggressive")

    # ── Restore stable ────────────────────────────────────────────
    print("\n\n>>> Restoring Stable Profile <<<")
    opener = apply_profile(opener, "stable")
    print("Done.")

    print_final_report(ramp_stable, ramp_aggr, hold_stable, hold_aggr, comp_stable, comp_aggr)


if __name__ == "__main__":
    main()
