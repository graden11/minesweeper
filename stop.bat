@echo off
echo === Stopping HttpServer ===
wsl -e bash -c "cd /mnt/d/jetbrains/clion-project/httpserver && docker compose -f docker-compose.cpu.yml down"
echo Done.
