@echo off
cd /d E:\llama\NewChangeDirectory
echo Starting NCDService (60 second run)...
start /B NCDService.exe /agdb 2>agdb_stderr.txt >agdb_stdout.txt
echo PID started
timeout /t 60 /nobreak >nul
taskkill /F /IM NCDService.exe 2>nul
echo Process killed after 60 seconds
