@echo off
setlocal EnableExtensions

set "SRC_DIR=%~dp0"
set "DEST_DIR=%USERPROFILE%\Tools\bin"

if not "%~1"=="" set "DEST_DIR=%~1"

echo ================================================
echo NCD Deployment
echo ================================================
echo.

:: Check for executables
if not exist "%SRC_DIR%NewChangeDirectory.exe" (
    echo ERROR: "%SRC_DIR%NewChangeDirectory.exe" not found.
    echo Run build.bat first to compile.
    exit /b 1
)

if not exist "%SRC_DIR%NCDService.exe" (
    echo WARNING: "%SRC_DIR%NCDService.exe" not found.
    echo Service will not be deployed.
    set "HAS_SERVICE=0"
) else (
    set "HAS_SERVICE=1"
)

if not exist "%SRC_DIR%ncd.bat" (
    echo ERROR: "%SRC_DIR%ncd.bat" not found.
    exit /b 1
)

:: Get version info from the new executable
for /f "tokens=*" %%a in ('"%SRC_DIR%NewChangeDirectory.exe" /v 2^>^&1') do (
    set "NEW_VERSION=%%a"
)
echo New version: %NEW_VERSION%
echo.

:: Check if service is running
tasklist /FI "IMAGENAME eq NCDService.exe" 2>nul | find /I "NCDService.exe" >nul
if "%ERRORLEVEL%"=="0" (
    echo Service is currently running.
    
    :: Check version compatibility using agent mode
    echo Checking version compatibility...
    "%SRC_DIR%NewChangeDirectory.exe" /agent check --service-status >nul 2>&1
    if "%ERRORLEVEL%"=="0" (
        echo Requesting graceful service shutdown...
        "%SRC_DIR%NewChangeDirectory.exe" /agent quit 2>nul
        
        :: Wait for service to stop (max 10 seconds)
        set /a "WAIT_COUNT=0"
        :WAIT_LOOP
        timeout /t 1 /nobreak >nul
        tasklist /FI "IMAGENAME eq NCDService.exe" 2>nul | find /I "NCDService.exe" >nul
        if "%ERRORLEVEL%"=="0" (
            set /a "WAIT_COUNT+=1"
            if %WAIT_COUNT% LSS 10 goto WAIT_LOOP
            echo Warning: Service did not stop gracefully, forcing termination...
            taskkill /F /IM NCDService.exe >nul 2>&1
            timeout /t 1 /nobreak >nul
        ) else (
            echo Service stopped gracefully.
        )
    ) else (
        echo Service not responding, forcing termination...
        taskkill /F /IM NCDService.exe >nul 2>&1
        timeout /t 1 /nobreak >nul
    )
    echo.
)

:: Create destination directory
if not exist "%DEST_DIR%" (
    echo Creating destination directory: %DEST_DIR%
    mkdir "%DEST_DIR%" || (
        echo ERROR: Could not create "%DEST_DIR%".
        exit /b 1
    )
)

:: Backup existing files
if exist "%DEST_DIR%\NewChangeDirectory.exe" (
    echo Backing up existing NewChangeDirectory.exe...
    copy /Y "%DEST_DIR%\NewChangeDirectory.exe" "%DEST_DIR%\NewChangeDirectory.exe.old" >nul
)
if exist "%DEST_DIR%\NCDService.exe" (
    echo Backing up existing NCDService.exe...
    copy /Y "%DEST_DIR%\NCDService.exe" "%DEST_DIR%\NCDService.exe.old" >nul
)

:: Copy new files
echo.
echo Deploying files...
copy /Y "%SRC_DIR%NewChangeDirectory.exe" "%DEST_DIR%\" >nul || (
    echo ERROR: Failed to copy NewChangeDirectory.exe.
    exit /b 1
)

if "%HAS_SERVICE%"=="1" (
    copy /Y "%SRC_DIR%NCDService.exe" "%DEST_DIR%\" >nul || (
        echo WARNING: Failed to copy NCDService.exe.
    )
)

copy /Y "%SRC_DIR%ncd.bat" "%DEST_DIR%\" >nul || (
    echo ERROR: Failed to copy ncd.bat.
    exit /b 1
)

:: Copy ncd_service.bat if it exists
if exist "%SRC_DIR%ncd_service.bat" (
    copy /Y "%SRC_DIR%ncd_service.bat" "%DEST_DIR%\" >nul
)

echo.
echo ================================================
echo Deployment Summary
echo ================================================
echo Destination: %DEST_DIR%
echo   [OK] NewChangeDirectory.exe
if "%HAS_SERVICE%"=="1" echo   [OK] NCDService.exe
echo   [OK] ncd.bat

:: Start the new service
if "%HAS_SERVICE%"=="1" (
    echo.
    echo Starting NCD Service...
    start "" "%DEST_DIR%\NCDService.exe"
    timeout /t 2 /nobreak >nul
    
    :: Verify service started
    tasklist /FI "IMAGENAME eq NCDService.exe" 2>nul | find /I "NCDService.exe" >nul
    if "%ERRORLEVEL%"=="0" (
        echo   [OK] Service started successfully
    ) else (
        echo   [WARNING] Service may not have started. Check manually.
    )
)

echo.
echo ================================================
echo Deployment complete!
echo ================================================

endlocal
exit /b 0
