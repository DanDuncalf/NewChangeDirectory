@echo off
::
:: ncd_service.bat  --  NCD State Service launcher for Windows
::
:: Usage:
::   ncd_service.bat start    - Start the service
::   ncd_service.bat stop     - Stop the service
::   ncd_service.bat status   - Check if service is running
::   ncd_service.bat restart  - Restart the service
::

setlocal enabledelayedexpansion

set SERVICE_EXE=NCDService.exe
set ACTION=%1

if "%ACTION%"=="" set ACTION=status

if "%ACTION%"=="start" goto :start
if "%ACTION%"=="status" goto :status
if "%ACTION%"=="stop" goto :stop
if "%ACTION%"=="restart" goto :restart
if "%ACTION%"=="help" goto :help

echo Unknown action: %ACTION%
goto :help

:start
if not exist %SERVICE_EXE% (
    echo ERROR: %SERVICE_EXE% not found
    echo Please build the project first with build.bat
    exit /b 1
)
%SERVICE_EXE% start
goto :end

:status
%SERVICE_EXE% status
goto :end

:stop
%SERVICE_EXE% stop
goto :end

:restart
call :stop
timeout /t 1 /nobreak >nul
call :start
goto :end

:help
echo NCD State Service Launcher
echo.
echo Usage: ncd_service.bat [command]
echo.
echo Commands:
echo   start    - Start the NCD State Service
echo   stop     - Stop the NCD State Service
echo   restart  - Restart the NCD State Service
echo   status   - Check if service is running
echo   help     - Show this help message
echo.
echo The NCD State Service keeps directory databases hot in memory
echo for faster navigation. It is optional - NCD works without it.
echo.

:end
endlocal
