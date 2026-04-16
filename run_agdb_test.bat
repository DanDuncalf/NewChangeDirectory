@echo off
cd /d E:\llama\NewChangeDirectory
echo Starting NCDService...
start /B NCDService.exe /agdb 2>agdb_stderr.txt >agdb_stdout.txt
echo PID started
timeout /t 25 /nobreak >nul
taskkill /F /IM NCDService.exe 2>nul
echo Process killed
