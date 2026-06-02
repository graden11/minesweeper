#!/bin/bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

MODE="cpu"
SKIP_MODEL_CHECK=false
BUILD_ONLY=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --gpu|gpu) MODE="gpu"; shift ;;
        --cpu|cpu) MODE="cpu"; shift ;;
        --skip-model-check) SKIP_MODEL_CHECK=true; shift ;;
        --build-only) BUILD_ONLY=true; shift ;;
        -h|--help)
            echo "Usage: ./start.sh [cpu|gpu] [--skip-model-check] [--build-only]"
            echo "  cpu               CPU-only mode (default, works everywhere)"
            echo "  gpu               GPU mode (requires NVIDIA GPU + CUDA 12.6)"
            echo "  --skip-model-check  Skip model file verification"
            echo "  --build-only        Build but don't start containers"
            exit 0
            ;;
        *) echo -e "${RED}Unknown: $1${NC}"; echo "Usage: ./start.sh [cpu|gpu]"; exit 1 ;;
    esac
done

if [ ! -f "CMakeLists.txt" ]; then
    echo -e "${RED}Error: Must be run from the project root directory.${NC}"
    echo "  cd /path/to/httpserver && ./start.sh"
    exit 1
fi

echo ""
echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN}  Kama-HTTPServer — One-Click Deploy (${MODE^^})${NC}"
echo -e "${CYAN}============================================${NC}"

# 1. Check prerequisites
echo -e "\n${CYAN}[1/5]${NC} Checking prerequisites..."

if ! command -v docker &>/dev/null; then
    echo -e "${RED}Docker not found. Install:${NC}"
    echo "  curl -fsSL https://get.docker.com | bash"
    echo "  sudo apt install -y docker-compose-plugin"
    exit 1
fi

if ! docker compose version &>/dev/null; then
    echo -e "${RED}Docker Compose v2 required.${NC}"
    echo "  sudo apt install -y docker-compose-plugin"
    exit 1
fi

echo -e "  ${GREEN}Docker:${NC} $(docker -v 2>&1)"
echo -e "  ${GREEN}Compose:${NC} $(docker compose version 2>&1)"

if [ "$MODE" = "gpu" ]; then
    if ! docker run --rm --gpus all nvidia/cuda:12.6.0-base-ubuntu22.04 nvidia-smi &>/dev/null; then
        echo -e "  ${YELLOW}Warning: GPU not accessible. Install NVIDIA Container Toolkit.${NC}"
        echo "  https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html"
    else
        echo -e "  ${GREEN}GPU:${NC} Available"
    fi
fi

# 2. Check model files
MODEL_DIR="models"

if [ "$SKIP_MODEL_CHECK" = false ]; then
    echo -e "\n${CYAN}[2/5]${NC} Checking model files..."
    MISSING=()

    [ ! -f "$MODEL_DIR/resnet50_classification.onnx" ] && MISSING+=("resnet50_classification.onnx (~98 MB)")
    [ ! -f "$MODEL_DIR/imagenet_classes.txt" ]   && MISSING+=("imagenet_classes.txt (~10 KB)")

    # GPU-only: warn but don't block
    if [ "$MODE" = "gpu" ]; then
        [ ! -f "$MODEL_DIR/resnet50_classification.engine" ] && \
            echo -e "  ${YELLOW}Note:${NC} resnet50_classification.engine not found (GPU FP16, optional)"
    fi

    if [ ${#MISSING[@]} -gt 0 ]; then
        echo -e "\n${YELLOW}  Missing required model files:${NC}"
        for f in "${MISSING[@]}"; do
            echo -e "    ${RED}- $f${NC}"
        done
        echo ""
        echo "  Place them in: $MODEL_DIR/"
        echo "  Then re-run:   ./start.sh $MODE"
        echo ""
        echo "  To skip this check: ./start.sh $MODE --skip-model-check"
        exit 1
    fi
    echo -e "  ${GREEN}All required model files found.${NC}"
else
    echo -e "\n${CYAN}[2/5]${NC} Skipping model check (--skip-model-check)."
fi

# 3. Build
echo -e "\n${CYAN}[3/5]${NC} Building project (${MODE^^} mode)..."

mkdir -p build && cd build

if [ "$MODE" = "cpu" ]; then
    cmake .. -DENABLE_TENSORRT=OFF
else
    cmake .. -DENABLE_TENSORRT=ON
fi
make -j$(nproc)
cd ..

echo -e "  ${GREEN}Build complete.${NC}"

if [ "$BUILD_ONLY" = true ]; then
    echo -e "\n${GREEN}Build-only mode. Binary at: build/simple_server${NC}"
    echo "Run: cd build && ./simple_server -c ../WebApps/InferenceServer/config.json"
    exit 0
fi

# 4. Start services
echo -e "\n${CYAN}[4/5]${NC} Starting Docker services..."

if [ "$MODE" = "cpu" ]; then
    docker compose -f docker-compose.cpu.yml up -d --build
    COMPOSE_FILE="docker-compose.cpu.yml"
else
    docker compose up -d --build
    COMPOSE_FILE="docker-compose.yml"
fi

# 5. Health check
echo -e "\n${CYAN}[5/5]${NC} Waiting for service to be ready..."

MAX_WAIT=90
WAITED=0
HEALTHY=false

while [ $WAITED -lt $MAX_WAIT ]; do
    if curl -s -o /dev/null -w "%{http_code}" http://localhost/health 2>/dev/null | grep -q "200"; then
        HEALTHY=true
        break
    fi
    sleep 3
    WAITED=$((WAITED + 3))
    echo "  Waiting for service... (${WAITED}s)"
done

if [ "$HEALTHY" = true ]; then
    echo -e "  ${GREEN}Service is healthy!${NC}"
    echo ""
    curl -s http://localhost/ready
else
    echo -e "  ${YELLOW}Health check timeout (${MAX_WAIT}s). Check logs:${NC}"
    echo "  docker compose -f $COMPOSE_FILE logs httpserver"
fi

# Done
echo ""
echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}  Kama-HTTPServer is running!${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""
echo "  Endpoints:"
echo "    http://localhost/              Login page"
echo "    http://localhost/health        Health check"
echo "    http://localhost/metrics       Prometheus metrics"
echo "    http://localhost/models        Model list"
echo ""
echo "  Quick test:"
echo "    curl http://localhost/health"
echo "    curl -X POST http://localhost/register -H 'Content-Type: application/json' -d '{\"username\":\"demo\",\"password\":\"demo\"}'"
echo "    curl -X POST http://localhost/predict -H 'Content-Type: application/json' -d '{\"image_path\":\"/app/models/cat.jpg\"}'"
echo ""
echo "  Stop:  ./stop.sh $MODE"
echo "  Logs:  docker compose -f $COMPOSE_FILE logs -f httpserver"
echo ""
