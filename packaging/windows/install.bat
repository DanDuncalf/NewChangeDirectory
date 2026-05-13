@echo off
setlocal enabledelayedexpansion

:: ========================================================================
:: NCD Windows Installer
:: ========================================================================
:: Usage: install.bat [destination_path]
:: Default: %%LOCALAPPDATA%%\NCD\bin
::
:: This script installs NCD binaries and wrapper scripts to the
:: specified directory and optionally adds it to the user PATH.
:: ========================================================================

set "DEST_DIR=%~1"
if "%DEST_DIR%"=="" set "DEST_DIR=%LOCALAPPDATA%\NCD\bin"

echo ========================================
echo NCD Installer for Windows
echo ========================================
echo.
echo Install directory: %DEST_DIR%
echo.

:: Detect architecture
set "HOST_ARCH=x64"
if "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "HOST_ARCH=arm64"

:: Detect package architecture from included binaries
set "PKG_ARCH=x64"
if exist "NewChangeDirectory_arm64.exe" set "PKG_ARCH=arm64"

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
        echo ERROR: Failed to create directory. Run as Administrator?
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

echo   [OK] Binaries installed
echo.

:: Add to user PATH if not already present
echo Checking PATH...
for /f "tokens=*" %%a in ('powershell -NoProfile -Command "[Environment]::GetEnvironmentVariable('PATH','User')"') do set "USER_PATH=%%a"

echo %USER_PATH% | find /I "%DEST_DIR%" >nul
if errorlevel 1 (
    echo Adding %DEST_DIR% to user PATH...
    powershell -NoProfile -Command "[Environment]::SetEnvironmentVariable('PATH', [Environment]::GetEnvironmentVariable('PATH','User') + ';%DEST_DIR%', 'User')"
    echo   [OK] Added to PATH
    echo.
    echo IMPORTANT: Restart your command prompt for PATH changes to take effect.
) else (
    echo   [OK] Already in PATH
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
echo To uninstall, simply delete the files from:
echo   %DEST_DIR%
echo.

endlocal
exit /b 0
