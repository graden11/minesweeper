#!/bin/bash
set -e

MODE="${1:-gpu}"

if [ "$MODE" != "gpu" ] && [ "$MODE" != "cpu" ]; then
    echo "Usage: ./build.sh [gpu|cpu]"
    echo "  gpu  - Build with TensorRT GPU inference (RTX 5060)"
    echo "  cpu  - Build CPU-only for cloud deployment"
    exit 1
fi

echo "=== Building in ${MODE^^} mode ==="

mkdir -p build && cd build

if [ "$MODE" = "cpu" ]; then
    cmake .. -DENABLE_TENSORRT=OFF
    make -j$(nproc)
    cd ..
    echo "=== Starting with docker-compose.cpu.yml ==="
    docker compose -f docker-compose.cpu.yml up -d --build
else
    cmake .. -DENABLE_TENSORRT=ON
    make -j$(nproc)
    cd ..
    echo "=== Starting with docker-compose.yml ==="
    docker compose up -d --build
fi

echo "=== Done ==="
echo "Test: curl http://localhost/predict -H 'Content-Type: application/json' -d '{\"image_path\":\"/app/models/cat.jpg\"}'"
