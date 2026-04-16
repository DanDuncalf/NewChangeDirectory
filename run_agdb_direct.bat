@echo off
cd /d E:\llama\NewChangeDirectory
echo Starting NCDService directly (will run for 30 seconds)...
timeout /t 1 >nul
NCDService.exe /agdb 2>&1
echo Service exited or was terminated
