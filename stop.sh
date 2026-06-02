#!/bin/bash

MODE="${1:-cpu}"

if [ "$MODE" = "gpu" ]; then
    COMPOSE_FILE="docker-compose.yml"
else
    COMPOSE_FILE="docker-compose.cpu.yml"
fi

echo "Stopping Kama-HTTPServer (${MODE^^})..."
docker compose -f "$COMPOSE_FILE" down

echo "Done."
echo ""
echo "To also remove data volumes (MySQL + Redis):"
echo "  docker compose -f $COMPOSE_FILE down -v"
