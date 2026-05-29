@echo off
echo === HttpServer Docker Compose ===
wsl -e bash -c "cd /mnt/d/jetbrains/clion-project/httpserver && docker compose -f docker-compose.cpu.yml up -d"
echo.
echo Done! Opening http://localhost
start http://localhost/
