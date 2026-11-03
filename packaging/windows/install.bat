@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ==========================================================================
:: NCD Windows Installer
:: ==========================================================================
:: Usage:
::   install.bat [destination]         Interactive install
::   install.bat /quiet [destination]  Non-interactive: all-users install into
::                                     %%PROGRAMFILES%%\NCD\bin, no prompts
::
:: Standard install layout:
::   Binaries + wrappers   %%PROGRAMFILES%%\NCD\bin   (all users, Machine PATH)
::                         %%LOCALAPPDATA%%\NCD\bin  (current user, User PATH)
::   Data (databases, etc) %%PROGRAMDATA%%\NCD       (system mode, the default)
::                         %%LOCALAPPDATA%%\NCD      (user mode)
::
:: Data is never installed into (or put on PATH from) the binary directory.
::
:: Environment overrides (inherited across the elevation re-launch, so
:: deploy.bat can drive this script non-interactively):
::   NCD_SRC_DIR         Directory holding the binaries/completions to install
::   NCD_INSTALL_SCOPE   user | system
::   NCD_INSTALL_DEST    Explicit destination directory
::   NCD_INSTALL_QUIET   1 = skip all prompts (system scope)
:: ==========================================================================

:: ---- Source directory (package root, or repo root via NCD_SRC_DIR) -------
:: NOTE: `shift` also rotates %0, so capture the script location before
:: parsing arguments; otherwise %~dp0 later points at an argument.
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_PATH=%~f0"
set "SRC_DIR=%SCRIPT_DIR%"
if defined NCD_SRC_DIR set "SRC_DIR=%NCD_SRC_DIR%"

:: ---- Elevation / architecture detection ---------------------------------
net session >nul 2>&1
set "IS_ELEVATED=0"
if %errorlevel% == 0 set "IS_ELEVATED=1"

set "HOST_ARCH=x64"
if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "HOST_ARCH=arm64"
if /I "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "HOST_ARCH=arm64"

:: Install the build matching the host. A source tree (the repo after
:: Create-Packages.ps1) can hold both builds; packages hold only one.
set "PKG_ARCH=%HOST_ARCH%"
if /I "%HOST_ARCH%"=="arm64" if not exist "%SRC_DIR%NewChangeDirectory_arm64.exe" set "PKG_ARCH=x64"
if /I "%HOST_ARCH%"=="x64" if not exist "%SRC_DIR%NewChangeDirectory.exe" if exist "%SRC_DIR%NewChangeDirectory_arm64.exe" set "PKG_ARCH=arm64"

:: ---- Options ------------------------------------------------------------
set "OPT_SCOPE="
set "OPT_DEST="
set "QUIET=0"
if defined NCD_INSTALL_SCOPE set "OPT_SCOPE=%NCD_INSTALL_SCOPE%"
if defined NCD_INSTALL_DEST  set "OPT_DEST=%NCD_INSTALL_DEST%"
if "%NCD_INSTALL_QUIET%"=="1" set "QUIET=1"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="/quiet"        ( set "QUIET=1" & shift & goto parse_args )
if /i "%~1"=="-quiet"        ( set "QUIET=1" & shift & goto parse_args )
if /i "%~1"=="/scope:user"   ( set "OPT_SCOPE=user" & shift & goto parse_args )
if /i "%~1"=="/scope:system" ( set "OPT_SCOPE=system" & shift & goto parse_args )
if not defined OPT_DEST ( set "OPT_DEST=%~1" & shift & goto parse_args )
shift
goto parse_args
:args_done

:: ---- Choose scope -------------------------------------------------------
if defined OPT_SCOPE goto scope_set
if "%QUIET%"=="1" set "OPT_SCOPE=system"
if defined OPT_SCOPE goto scope_set
echo Install scope:
echo   [1] All users      (%%PROGRAMFILES%%\NCD\bin) -- requires Administrator  [default]
echo   [2] Current user   (%%LOCALAPPDATA%%\NCD\bin) -- no Admin required
set "SCOPE_CHOICE="
set /p SCOPE_CHOICE="Choice [1/2]: "
if "%SCOPE_CHOICE%"=="2" set "OPT_SCOPE=user"
if not defined OPT_SCOPE set "OPT_SCOPE=system"
:scope_set

if /i "%OPT_SCOPE%"=="user" (
    set "INSTALL_SCOPE=user"
    set "PATH_TARGET=User"
    set "REG_ROOT=HKCU"
    if not defined OPT_DEST set "OPT_DEST=%LOCALAPPDATA%\NCD\bin"
) else (
    set "INSTALL_SCOPE=system"
    set "PATH_TARGET=Machine"
    set "REG_ROOT=HKLM"
    if not defined OPT_DEST set "OPT_DEST=%PROGRAMFILES%\NCD\bin"
)
set "DEST_DIR=%OPT_DEST%"

:: ---- Elevate for an all-users install -----------------------------------
if /i "%INSTALL_SCOPE%"=="system" if "%IS_ELEVATED%"=="0" (
    echo.
    echo Requesting Administrator privileges for the all-users install...
    set "NCD_SRC_DIR=%SRC_DIR%"
    set "NCD_INSTALL_SCOPE=system"
    set "NCD_INSTALL_DEST=%DEST_DIR%"
    if "%QUIET%"=="1" set "NCD_INSTALL_QUIET=1"
    powershell -NoProfile -Command "$p = Start-Process -FilePath '%SCRIPT_PATH%' -Verb RunAs -Wait -PassThru; exit $p.ExitCode"
    exit /b !ERRORLEVEL!
)

echo ========================================
echo NCD Installer for Windows
echo ========================================
echo.
echo Install directory: %DEST_DIR%
echo Scope: %INSTALL_SCOPE%
echo Host architecture: %HOST_ARCH%
echo Package architecture: %PKG_ARCH%
echo Data directory: %PROGRAMDATA%\NCD (system mode) / %LOCALAPPDATA%\NCD (user mode)
echo.

:: ---- Validate source files ----------------------------------------------
if /I "%PKG_ARCH%"=="arm64" (
    set "EXE_SRC=%SRC_DIR%NewChangeDirectory_arm64.exe"
    set "SVC_SRC=%SRC_DIR%NCDService_arm64.exe"
) else (
    set "EXE_SRC=%SRC_DIR%NewChangeDirectory.exe"
    set "SVC_SRC=%SRC_DIR%NCDService.exe"
)

if not exist "%EXE_SRC%" (
    echo ERROR: "%EXE_SRC%" not found.
    echo Run build.bat first, or run install.bat from the extracted package folder.
    exit /b 1
)
if not exist "%SRC_DIR%ncd.bat" (
    echo ERROR: "%SRC_DIR%ncd.bat" not found.
    exit /b 1
)

:: ---- Warn if architectures don't match ----------------------------------
if /I not "%HOST_ARCH%"=="%PKG_ARCH%" (
    echo WARNING: Package architecture ^(%PKG_ARCH%^) does not match host ^(%HOST_ARCH%^).
    echo The binaries will be installed but may not run on this machine.
    echo.
    if "%QUIET%"=="1" (
        echo Continuing in quiet mode.
    ) else (
        set "CONFIRM="
        set /p CONFIRM="Continue anyway? [Y/N]: "
        if /I not "!CONFIRM!"=="Y" if /I not "!CONFIRM!"=="yes" (
            echo Installation cancelled.
            exit /b 1
        )
    )
    echo.
)

:: ---- Create destination directory ---------------------------------------
if not exist "%DEST_DIR%" (
    echo Creating directory: %DEST_DIR%
    mkdir "%DEST_DIR%" 2>nul
    if errorlevel 1 (
        echo ERROR: Failed to create "%DEST_DIR%".
        if %IS_ELEVATED%==0 echo Run as Administrator for an all-users install.
        exit /b 1
    )
)

:: ---- Stop any running service before overwriting ------------------------
tasklist /FI "IMAGENAME eq NCDService.exe" 2>nul | "%SystemRoot%\System32\findstr.exe" /I /C:"NCDService.exe" >nul
if not errorlevel 1 (
    echo Stopping running NCD service...
    if exist "%DEST_DIR%\NCDService.exe" "%DEST_DIR%\NCDService.exe" stop >nul 2>&1
    timeout /t 1 /nobreak >nul 2>&1
    taskkill /F /IM NCDService.exe >nul 2>&1
    timeout /t 1 /nobreak >nul 2>&1
)

:: ---- Copy binaries and wrappers -----------------------------------------
echo Installing binaries...
copy /Y "%EXE_SRC%" "%DEST_DIR%\NewChangeDirectory.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy NewChangeDirectory.exe.
    exit /b 1
)
copy /Y "%SVC_SRC%" "%DEST_DIR%\NCDService.exe" >nul
if errorlevel 1 echo   [WARN] Failed to copy NCDService.exe (service will not be available).
copy /Y "%SRC_DIR%ncd.bat" "%DEST_DIR%\ncd.bat" >nul
copy /Y "%SRC_DIR%ncd_service.bat" "%DEST_DIR%\ncd_service.bat" >nul

:: Copy completions if they exist
if exist "%SRC_DIR%completions" (
    if not exist "%DEST_DIR%\completions" mkdir "%DEST_DIR%\completions" 2>nul
    xcopy /Y /I /S "%SRC_DIR%completions\*" "%DEST_DIR%\completions\" >nul 2>&1
)

:: Copy MCP server if it exists
if exist "%SRC_DIR%mcp_server" (
    if not exist "%DEST_DIR%\mcp_server" mkdir "%DEST_DIR%\mcp_server" 2>nul
    xcopy /Y /I /S "%SRC_DIR%mcp_server\*" "%DEST_DIR%\mcp_server\" >nul 2>&1
)

echo   [OK] Binaries installed
echo.

:: ---- Register the binary directory on PATH ------------------------------
:: update-path.ps1 appends a whole entry (no substring matching) and keeps
:: the registry value kind, so existing %VAR% entries keep expanding.
echo Checking PATH...
if not exist "%SCRIPT_DIR%update-path.ps1" (
    echo   [WARN] update-path.ps1 not found next to install.bat; add %DEST_DIR% to PATH manually.
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%update-path.ps1" -Directory "%DEST_DIR%" -Scope %PATH_TARGET%
    if errorlevel 1 (
        echo   [WARN] Failed to update PATH. Add %DEST_DIR% manually.
    ) else (
        echo   IMPORTANT: Open a new terminal for PATH changes to take effect.
    )
)
echo.

:: ---- Optional: service auto-start ---------------------------------------
if "%QUIET%"=="1" goto skip_startup_prompt
echo How should NCD Service start automatically?
echo   [1] Logon startup -- runs when you log in (no Admin required)
echo   [2] Windows service -- runs at system boot, even before login (requires Admin)
echo   [3] No auto-start  -- start manually when needed
set "STARTUP_CHOICE="
set /p STARTUP_CHOICE="Choice [1/2/3]: "
if "%STARTUP_CHOICE%"=="1" (
    echo Adding NCD Service to startup...
    reg add "%REG_ROOT%\Software\Microsoft\Windows\CurrentVersion\Run" /v NCDService /t REG_SZ /d "\"%DEST_DIR%\NCDService.exe\" start" /f >nul 2>&1
    if errorlevel 1 (
        echo   [WARN] Failed to add startup entry. You may need to run as Administrator.
    ) else (
        echo   [OK] NCD Service will start at logon.
    )
)
if "%STARTUP_CHOICE%"=="2" (
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

:: ---- Optional: start service now ----------------------------------------
set "START_NOW="
echo.
echo Start NCD Service now?
echo   [1] Yes -- start service
echo   [2] No  -- start later with: ncd_service start
set /p START_NOW="Choice [1/2]: "
if "%START_NOW%"=="1" (
    echo Starting NCD Service...
    "%DEST_DIR%\NCDService.exe" start
)

:: ---- Optional: install MCP server ---------------------------------------
python --version >nul 2>&1
if errorlevel 1 (
    echo.
    echo Note: Python not found. Skipping MCP server installation.
    echo   To install later: pip install "%DEST_DIR%\mcp_server"
) else (
    echo.
    set "INSTALL_MCP="
    set /p INSTALL_MCP="Python detected. Install MCP server? [Y/N]: "
    if /I "!INSTALL_MCP!"=="Y" (
        pip install "%DEST_DIR%\mcp_server"
        if errorlevel 1 (
            echo   [WARN] MCP server installation failed. Try manually:
            echo     pip install "%DEST_DIR%\mcp_server"
        ) else (
            echo   [OK] MCP server installed. Run 'ncd-mcp-server' to verify.
            echo   Add this to your MCP client config ^(e.g., Claude Desktop^):
            echo     { "mcpServers": { "ncd": { "command": "ncd-mcp-server" ^} ^} ^}
        )
    )
)

:skip_startup_prompt

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
    echo   1. If installed as Windows service: '%DEST_DIR%\NCDService.exe' uninstall  ^(requires Admin^)
    echo   2. Delete files from: %DEST_DIR%
    echo   3. Remove %DEST_DIR% from the Machine PATH
    echo   4. Remove startup entry from Registry ^(HKLM\...\Run\NCDService^)
) else (
    echo To uninstall:
    echo   1. If installed as Windows service: '%DEST_DIR%\NCDService.exe' uninstall  ^(requires Admin^)
    echo   2. Delete the files from: %DEST_DIR%
    echo   3. Remove %DEST_DIR% from the User PATH
)
echo.

endlocal
exit /b 0
