#!/bin/bash
set -e

MODE="${1:-gpu}"

if [ "$MODE" != "gpu" ] && [ "$MODE" != "cpu" ]; then
    echo "Usage: ./build.sh [gpu|cpu]"
    echo "  gpu  - Build with TensorRT GPU inference (NVIDIA GPU required)"
    echo "  cpu  - Build CPU-only for cloud deployment"
    exit 1
fi

echo "=== Building ${MODE^^} mode (Docker multi-stage) ==="

if [ "$MODE" = "cpu" ]; then
    docker compose -f docker-compose.cpu.yml up -d --build
else
    docker compose up -d --build
fi

echo "=== Done ==="
echo "Test: curl http://localhost/predict -H 'Content-Type: application/json' -d '{\"image_path\":\"/app/models/cat.jpg\"}'"
