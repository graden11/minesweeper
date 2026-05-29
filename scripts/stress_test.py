#!/usr/bin/env python3
"""Stress test for /predict endpoint."""

import argparse
import base64
import json
import statistics
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import requests


def load_image_base64(path: str) -> str:
    with open(path, "rb") as f:
        return base64.b64encode(f.read()).decode("ascii")


def send_one(url: str, img_b64: str) -> dict:
    t0 = time.perf_counter()
    try:
        r = requests.post(
            url,
            json={"image_data": img_b64},
            timeout=30,
        )
        elapsed_ms = (time.perf_counter() - t0) * 1000
        return {
            "status": r.status_code,
            "elapsed_ms": elapsed_ms,
            "body": r.json() if r.status_code == 200 else r.text,
            "error": None,
        }
    except Exception as e:
        elapsed_ms = (time.perf_counter() - t0) * 1000
        return {
            "status": 0,
            "elapsed_ms": elapsed_ms,
            "body": None,
            "error": str(e),
        }


def main():
    parser = argparse.ArgumentParser(description="Stress test /predict")
    parser.add_argument("--url", default="http://localhost/predict")
    parser.add_argument("--image", default="cat.jpg", help="Path to test image")
    parser.add_argument("--concurrency", type=int, default=100)
    parser.add_argument("--requests", type=int, default=10000)
    args = parser.parse_args()

    if not Path(args.image).exists():
        print(f"Image not found: {args.image}")
        sys.exit(1)

    print(f"Loading image: {args.image}")
    img_b64 = load_image_base64(args.image)
    print(f"Encoded size: {len(img_b64)} chars (~{len(img_b64) * 3 // 4 / 1024:.0f} KB)")
    print(f"Target: {args.url}")
    print(f"Concurrency: {args.concurrency}  Requests: {args.requests}")
    print("-" * 60)

    latencies = []
    ok_count = 0
    fail_count = 0
    t_start = time.perf_counter()

    with ThreadPoolExecutor(max_workers=args.concurrency) as pool:
        futures = {pool.submit(send_one, args.url, img_b64): i for i in range(args.requests)}

        for i, f in enumerate(as_completed(futures), 1):
            result = f.result()
            latencies.append(result["elapsed_ms"])

            if result["status"] == 200:
                ok_count += 1
            else:
                fail_count += 1

            # Print every 10% progress
            if i % max(1, args.requests // 10) == 0:
                print(f"  [{i}/{args.requests}] ok={ok_count} fail={fail_count} "
                      f"latency={result['elapsed_ms']:.1f}ms")

    t_total = time.perf_counter() - t_start

    print("-" * 60)
    print("SUMMARY")
    print(f"  Total requests : {args.requests}")
    print(f"  OK             : {ok_count}")
    print(f"  Failed         : {fail_count}")
    print(f"  Total time     : {t_total:.2f}s")
    print(f"  QPS            : {args.requests / t_total:.1f}")

    if latencies:
        latencies.sort()
        n = len(latencies)
        print(f"  Latency (ms):")
        print(f"    min  : {latencies[0]:.1f}")
        print(f"    avg  : {sum(latencies) / n:.1f}")
        print(f"    max  : {latencies[-1]:.1f}")
        print(f"    p50  : {latencies[n // 2]:.1f}")
        print(f"    p95  : {latencies[int(n * 0.95)]:.1f}")
        print(f"    p99  : {latencies[int(n * 0.99)]:.1f}")


if __name__ == "__main__":
    main()
