@echo off
::
:: ncd_service.bat  --  NCD State Service launcher for Windows
::
:: Usage:
::   ncd_service.bat start    - Start the service
::   ncd_service.bat stop     - Stop the service (not implemented for simple version)
::   ncd_service.bat status   - Check if service is running
::

setlocal enabledelayedexpansion

set SERVICE_EXE=NCDService.exe
set ACTION=%1

if "%ACTION%"=="" set ACTION=status

if "%ACTION%"=="start" goto :start
if "%ACTION%"=="status" goto :status
if "%ACTION%"=="stop" goto :stop
if "%ACTION%"=="help" goto :help

echo Unknown action: %ACTION%
goto :help

:start
echo Starting NCD State Service...
if not exist %SERVICE_EXE% (
    echo ERROR: %SERVICE_EXE% not found
    echo Please build the project first with build.bat
    exit /b 1
)

:: Check if already running
:: Note: Simple check - in production, use proper process tracking
wmic process where "name='%SERVICE_EXE%'" get ProcessId 2>nul | findstr /r "[0-9]" >nul
if %errorlevel% == 0 (
    echo NCD State Service is already running
    exit /b 0
)

:: Start service in background
start /B %SERVICE_EXE%
timeout /t 1 /nobreak >nul

:: Verify it started
wmic process where "name='%SERVICE_EXE%'" get ProcessId 2>nul | findstr /r "[0-9]" >nul
if %errorlevel% == 0 (
    echo NCD State Service started successfully
) else (
    echo ERROR: Failed to start NCD State Service
    exit /b 1
)
goto :end

:status
wmic process where "name='%SERVICE_EXE%'" get ProcessId 2>nul | findstr /r "[0-9]" >nul
if %errorlevel% == 0 (
    echo NCD State Service is running
    
    :: Get process details
    for /f "tokens=*" %%a in ('wmic process where "name='%SERVICE_EXE%'" get ProcessId /value ^| find "="') do (
        echo PID: %%a
    )
) else (
    echo NCD State Service is not running
)
goto :end

:stop
echo Stopping NCD State Service...
:: Note: For simple version, we just kill the process
:: In production, use proper IPC shutdown
taskkill /F /IM %SERVICE_EXE% 2>nul
if %errorlevel% == 0 (
    echo NCD State Service stopped
) else (
    echo NCD State Service was not running or could not be stopped
)
goto :end

:help
echo NCD State Service Launcher
echo.
echo Usage: ncd_service.bat [command]
echo.
echo Commands:
echo   start   - Start the NCD State Service
echo   stop    - Stop the NCD State Service
echo   status  - Check if service is running
echo   help    - Show this help message
echo.
echo The NCD State Service keeps directory databases hot in memory
echo for faster navigation. It is optional - NCD works without it.
echo.

:end
endlocal
