@echo off
setlocal enabledelayedexpansion

:: ========================================================================
:: NCD Windows Installer
:: ========================================================================
:: Usage: install.bat [destination_path]
:: Default (per-user): %%LOCALAPPDATA%%\NCD\bin
::
:: This script installs NCD binaries and wrapper scripts, adds to PATH,
:: and optionally configures the service to start at logon.
:: ========================================================================

:: Detect if already running elevated
net session >nul 2>&1
set "IS_ELEVATED=0"
if %errorlevel% == 0 set "IS_ELEVATED=1"

:: Detect architecture
set "HOST_ARCH=x64"
if "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "HOST_ARCH=arm64"

:: Detect package architecture from included binaries
set "PKG_ARCH=x64"
if exist "NewChangeDirectory_arm64.exe" set "PKG_ARCH=arm64"

echo ========================================
echo NCD Installer for Windows
echo ========================================
echo.

:: Ask for install scope (unless destination was provided on command line)
set "INSTALL_SCOPE=user"
if "%~1"=="" (
    echo Install scope:
    echo   [1] Current user only (%%LOCALAPPDATA%%\NCD\bin) -- no Admin required
    echo   [2] All users (%%PROGRAMFILES%%\NCD\bin) -- requires Administrator
    set /p SCOPE_CHOICE="Choice [1/2]: "
    if "!SCOPE_CHOICE!"=="2" (
        set "INSTALL_SCOPE=system"
    )
) else (
    set "DEST_DIR=%~1"
)

:: Determine destination and check elevation
if "%INSTALL_SCOPE%"=="system" (
    if "%DEST_DIR%"=="" set "DEST_DIR=%PROGRAMFILES%\NCD\bin"
    if %IS_ELEVATED%==0 (
        echo.
        echo Requesting Administrator privileges for system-wide installation...
        powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '%DEST_DIR%' -Verb RunAs"
        exit /b 0
    )
    set "PATH_TARGET=Machine"
    set "REG_ROOT=HKLM"
) else (
    if "%DEST_DIR%"=="" set "DEST_DIR=%LOCALAPPDATA%\NCD\bin"
    set "PATH_TARGET=User"
    set "REG_ROOT=HKCU"
)

echo Install directory: %DEST_DIR%
echo Scope: %INSTALL_SCOPE%
echo Host architecture: %HOST_ARCH%
echo Package architecture: %PKG_ARCH%
echo.

:: Warn if architectures don't match
if /I not "%HOST_ARCH%"=="%PKG_ARCH%" (
    echo WARNING: Package architecture (%PKG_ARCH%) does not match host (%HOST_ARCH%).
    echo The binaries will be installed but may not run on this machine.
    echo.
    echo Continue anyway? [Y/N]
    set /p CONFIRM=
    if /I not "!CONFIRM!"=="Y" if /I not "!CONFIRM!"=="yes" (
        echo Installation cancelled.
        exit /b 1
    )
    echo.
)

:: Create destination directory
if not exist "%DEST_DIR%" (
    echo Creating directory: %DEST_DIR%
    mkdir "%DEST_DIR%" 2>nul
    if errorlevel 1 (
        echo ERROR: Failed to create directory.
        if %IS_ELEVATED%==0 echo Run as Administrator for system-wide install.
        exit /b 1
    )
)

:: Stop any running service before overwriting
if exist "%DEST_DIR%\NCDService.exe" (
    echo Stopping any running NCD service...
    taskkill /F /IM NCDService.exe >nul 2>&1
    timeout /t 1 /nobreak >nul 2>&1
)

:: Copy binaries
echo Installing binaries...

if /I "%PKG_ARCH%"=="arm64" (
    copy /Y "NewChangeDirectory_arm64.exe" "%DEST_DIR%\NewChangeDirectory.exe" >nul
    copy /Y "NCDService_arm64.exe" "%DEST_DIR%\NCDService.exe" >nul
) else (
    copy /Y "NewChangeDirectory.exe" "%DEST_DIR%\NewChangeDirectory.exe" >nul
    copy /Y "NCDService.exe" "%DEST_DIR%\NCDService.exe" >nul
)

copy /Y "ncd.bat" "%DEST_DIR%\ncd.bat" >nul
copy /Y "ncd_service.bat" "%DEST_DIR%\ncd_service.bat" >nul

:: Copy completions if they exist
if exist "completions" (
    if not exist "%DEST_DIR%\completions" mkdir "%DEST_DIR%\completions" 2>nul
    xcopy /Y /I /S "completions\*" "%DEST_DIR%\completions\" >nul 2>&1
)

:: Copy MCP server if it exists
if exist "mcp_server" (
    if not exist "%DEST_DIR%\mcp_server" mkdir "%DEST_DIR%\mcp_server" 2>nul
    xcopy /Y /I /S "mcp_server\*" "%DEST_DIR%\mcp_server\" >nul 2>&1
)

echo   [OK] Binaries installed
echo.

:: Add to PATH if not already present
echo Checking PATH...
for /f "tokens=*" %%a in ('powershell -NoProfile -Command "[Environment]::GetEnvironmentVariable('PATH','%PATH_TARGET%')"') do set "CURRENT_PATH=%%a"

echo %CURRENT_PATH% | find /I "%DEST_DIR%" >nul
if errorlevel 1 (
    echo Adding %DEST_DIR% to %PATH_TARGET% PATH...
    powershell -NoProfile -Command "[Environment]::SetEnvironmentVariable('PATH', [Environment]::GetEnvironmentVariable('PATH','%PATH_TARGET%') + ';%DEST_DIR%', '%PATH_TARGET%')"
    echo   [OK] Added to PATH
    echo.
    echo IMPORTANT: Restart your command prompt for PATH changes to take effect.
) else (
    echo   [OK] Already in PATH
)

:: Optional: Add to startup or register as Windows service
echo.
echo How should NCD Service start automatically?
echo   [1] Logon startup -- runs when you log in (no Admin required)
echo   [2] Windows service -- runs at system boot, even before login (requires Admin)
echo   [3] No auto-start  -- start manually when needed
set /p STARTUP_CHOICE="Choice [1/2/3]: "
if "!STARTUP_CHOICE!"=="1" (
    echo Adding NCD Service to startup...
    reg add "%REG_ROOT%\Software\Microsoft\Windows\CurrentVersion\Run" /v NCDService /t REG_SZ /d "\"%DEST_DIR%\NCDService.exe\" start" /f >nul 2>&1
    if errorlevel 1 (
        echo   [WARN] Failed to add startup entry. You may need to run as Administrator.
    ) else (
        echo   [OK] NCD Service will start at logon.
    )
)
if "!STARTUP_CHOICE!"=="2" (
    if %IS_ELEVATED%==0 (
        echo.
        echo Windows service registration requires Administrator privileges.
        echo Requesting elevation...
        powershell -NoProfile -Command "Start-Process -FilePath '%DEST_DIR%\NCDService.exe' -ArgumentList 'install' -Verb RunAs -Wait"
    ) else (
        echo Registering NCD Service with Windows Service Control Manager...
        "%DEST_DIR%\NCDService.exe" install
    )
)

:: Optional: Start service now
echo.
echo Start NCD Service now?
echo   [1] Yes -- start service
echo   [2] No  -- start later with: ncd_service start
set /p START_NOW="Choice [1/2]: "
if "!START_NOW!"=="1" (
    echo Starting NCD Service...
    "%DEST_DIR%\NCDService.exe" start
)

:: Optional: Install MCP server
python --version >nul 2>&1
if errorlevel 1 (
    echo.
    echo Note: Python not found. Skipping MCP server installation.
    echo   To install later: pip install "%DEST_DIR%\mcp_server"
) else (
    echo.
    echo Python detected. Install MCP server? [Y/N]
    set /p INSTALL_MCP=
    if /I "!INSTALL_MCP!"=="Y" (
        pip install "%DEST_DIR%\mcp_server"
        if errorlevel 1 (
            echo   [WARN] MCP server installation failed. Try manually:
            echo     pip install "%DEST_DIR%\mcp_server"
        ) else (
            echo   [OK] MCP server installed. Run 'ncd-mcp-server' to verify.
            echo   Add this to your MCP client config (e.g., Claude Desktop):
            echo     { "mcpServers": { "ncd": { "command": "ncd-mcp-server" ^} ^} ^}
        )
    )
)

echo.
echo ========================================
echo Installation Complete
echo ========================================
echo.
echo Installed to: %DEST_DIR%
echo.
echo Usage:
echo   ncd ^<search^>        - Navigate to a directory
echo   ncd_service start    - Start the resident service
echo   ncd -?               - Show help
echo.
if "%INSTALL_SCOPE%"=="system" (
    echo To uninstall:
    echo   1. If installed as Windows service: '%DEST_DIR%\NCDService.exe' uninstall  (requires Admin)
    echo   2. Delete files from: %DEST_DIR%
    echo   3. Remove from PATH via System Environment Variables
    echo   4. Remove startup entry from Registry (HKLM\...\Run\NCDService)
) else (
    echo To uninstall:
    echo   1. If installed as Windows service: '%DEST_DIR%\NCDService.exe' uninstall  (requires Admin)
    echo   2. Delete the files from: %DEST_DIR%
)
echo.

endlocal
exit /b 0
