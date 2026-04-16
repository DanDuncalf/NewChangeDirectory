@echo off
cd /d E:\llama\NewChangeDirectory
echo Starting service with debug mode... > debug_capture.txt 2>&1
NCDService.exe /agdb >> debug_capture.txt 2>&1 &
timeout /t 5 /nobreak > nul
taskkill /f /im NCDService.exe > nul 2>&1
