@echo off
powershell -NoProfile -Command "& cmd /c exit 42; exit $LASTEXITCODE"
echo ERRORLEVEL=%ERRORLEVEL%
