@echo off
echo === Full Rebuild & Start (CPU) ===
wsl -e bash -c "cd /mnt/d/jetbrains/clion-project/httpserver && ./build.sh cpu"
echo.
echo Done! Opening http://localhost
start http://localhost/
