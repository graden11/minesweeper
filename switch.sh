#!/bin/bash
set -e

MODE="${1:-gpu}"

if [ "$MODE" != "gpu" ] && [ "$MODE" != "cpu" ]; then
    echo "Usage: ./switch.sh [gpu|cpu]"
    echo "  gpu  - GPU mode with TensorRT (NVIDIA GPU required)"
    echo "  cpu  - CPU-only mode for cloud deployment"
    exit 1
fi

echo "=== Switching to ${MODE^^} mode ==="

# Stop all httpserver containers
docker compose -f docker-compose.yml down 2>/dev/null || true
docker compose -f docker-compose.cpu.yml down 2>/dev/null || true

if [ "$MODE" = "cpu" ]; then
    docker compose -f docker-compose.cpu.yml up -d --build
else
    docker compose up -d --build
fi

echo "=== Done, running in ${MODE^^} mode ==="
