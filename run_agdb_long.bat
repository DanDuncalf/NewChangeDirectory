@echo off
cd /d E:\llama\NewChangeDirectory
echo Starting NCDService (45 second run)...
start /B NCDService.exe /agdb 2>agdb_stderr.txt >agdb_stdout.txt
echo PID started
timeout /t 45 /nobreak >nul
taskkill /F /IM NCDService.exe 2>nul
echo Process killed after 45 seconds
