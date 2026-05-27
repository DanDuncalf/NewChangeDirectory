@echo off
::
:: ncd_service.bat  --  NCD State Service launcher for Windows
::
:: Usage:
::   ncd_service.bat start [-logN] [-init [drives]]   - Start the service
::   ncd_service.bat stop                             - Stop the service
::   ncd_service.bat status                           - Check if service is running
::   ncd_service.bat restart [-logN] [-init [drives]] - Restart the service
::
:: All extra arguments after the command are forwarded to NCDService.exe.
:: Examples:
::   ncd_service.bat start -log2
::   ncd_service.bat start -init C,D,E -log2

setlocal enabledelayedexpansion

set SERVICE_EXE=NCDService.exe
set ACTION=%1

:: Save all extra args after the action
set EXTRA_ARGS=
shift
:parse_args
if not "%1"=="" (
    set EXTRA_ARGS=!EXTRA_ARGS! %1
    shift
    goto :parse_args
)

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
%SERVICE_EXE% start%EXTRA_ARGS%
goto :end

:status
%SERVICE_EXE% status%EXTRA_ARGS%
goto :end

:stop
%SERVICE_EXE% stop%EXTRA_ARGS%
goto :end

:restart
call :stop_with_args %EXTRA_ARGS%
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1
call :start_with_args %EXTRA_ARGS%
goto :end

:stop_with_args
%SERVICE_EXE% stop %*
goto :eof

:start_with_args
if not exist %SERVICE_EXE% (
    echo ERROR: %SERVICE_EXE% not found
    exit /b 1
)
%SERVICE_EXE% start %*
goto :eof

:help
echo NCD State Service Launcher
echo.
echo Usage: ncd_service.bat [command] [options...]
echo.
echo Commands:
echo   start    - Start the NCD State Service
echo   stop     - Stop the NCD State Service
echo   restart  - Restart the NCD State Service
echo   status   - Check if service is running
echo   help     - Show this help message
echo.
echo Options (forwarded to NCDService.exe):
echo   -log0..-log5  - Enable service logging (recommended: -log2)
echo   -init [drives] - Initialize database on startup
echo   --user-mode    - Run in per-user mode
echo   -conf ^<path^>   - Custom metadata file path
echo.
echo Examples:
echo   ncd_service.bat start -log2
echo   ncd_service.bat start -log2 -init C,D
echo.
echo The NCD State Service keeps directory databases hot in memory
echo for faster navigation. It is optional - NCD works without it.
echo.

:end
endlocal
