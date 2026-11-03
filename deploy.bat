@echo off
setlocal EnableExtensions

:: ==========================================================================
:: deploy.bat  --  local/development deployment for Windows
:: ==========================================================================
:: Installs the repo build into the standard all-users location by delegating
:: to packaging\windows\install.bat /quiet, then (re)starts the NCD service.
::
::   Binaries + wrappers : %PROGRAMFILES%\NCD\bin   (added to Machine PATH)
::   Data                : %PROGRAMDATA%\NCD        (never on PATH)
::
:: Legacy pre-standardisation deployments put the binaries in
:: %PROGRAMDATA%\NCD; those executables are removed here while the data
:: files are left untouched.
::
:: Requires Administrator -- the script elevates itself.
:: ==========================================================================

set "SRC_DIR=%~dp0"
set "INSTALL_BAT=%SRC_DIR%packaging\windows\install.bat"
set "BIN_DIR=%PROGRAMFILES%\NCD\bin"
set "DATA_DIR=%PROGRAMDATA%\NCD"

:: ---- Validate the build inputs ------------------------------------------
for %%F in (NewChangeDirectory.exe NCDService.exe ncd.bat ncd_service.bat) do (
    if not exist "%SRC_DIR%%%F" (
        echo ERROR: "%SRC_DIR%%%F" not found.
        echo Run build.bat first to compile.
        exit /b 1
    )
)
if not exist "%INSTALL_BAT%" (
    echo ERROR: "%INSTALL_BAT%" not found.
    exit /b 1
)

:: ---- Elevate ------------------------------------------------------------
net session >nul 2>&1
if errorlevel 1 (
    echo Requesting Administrator privileges...
    set "NCD_SRC_DIR=%SRC_DIR%"
    powershell -NoProfile -Command "$p = Start-Process -FilePath '%~f0' -Verb RunAs -Wait -PassThru; exit $p.ExitCode"
    if errorlevel 1 (
        echo.
        echo Deployment cancelled or elevation failed.
        exit /b 1
    )
    exit /b 0
)

echo ================================================
echo NCD Deployment (all users)
echo ================================================
echo.

:: ---- Application Verifier IFEO cleanup ----------------------------------
:: Agents enable the verifier during debugging; the key adds ~6 s to every
:: launch of NewChangeDirectory.exe and survives rebuilds and reboots.
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\NewChangeDirectory.exe" >nul 2>&1
if "%ERRORLEVEL%"=="0" (
    echo WARNING: Application Verifier IFEO key detected; removing it.
    reg delete "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\NewChangeDirectory.exe" /f >nul 2>&1
    if errorlevel 1 (
        echo   [WARN] Could not remove the IFEO key.
    ) else (
        echo   [OK] IFEO key removed.
    )
    echo.
)

:: ---- Stop a running service before replacing its binaries --------------
tasklist /FI "IMAGENAME eq NCDService.exe" 2>nul | "%SystemRoot%\System32\findstr.exe" /I /C:"NCDService.exe" >nul
if not errorlevel 1 (
    echo Stopping running NCD service...
    if exist "%BIN_DIR%\NCDService.exe" "%BIN_DIR%\NCDService.exe" stop >nul 2>&1
    if exist "%DATA_DIR%\NCDService.exe" "%DATA_DIR%\NCDService.exe" stop >nul 2>&1
    call :wait_for_service_stop
    echo.
)

:: ---- Install: delegate every copy/PATH decision to install.bat ----------
echo Deploying to %BIN_DIR% ...
set "NCD_SRC_DIR=%SRC_DIR%"
call "%INSTALL_BAT%" /quiet
if errorlevel 1 (
    echo.
    echo ERROR: install.bat failed. Nothing was deployed.
    exit /b 1
)
echo.

:: ---- Remove legacy binaries from the data directory ---------------------
if exist "%DATA_DIR%" (
    call :remove_legacy "NewChangeDirectory.exe"
    call :remove_legacy "NCDService.exe"
    call :remove_legacy "ncd.bat"
    call :remove_legacy "ncd_service.bat"
    call :remove_legacy "NewChangeDirectory.exe.old"
    call :remove_legacy "NCDService.exe.old"
    call :remove_legacy "ncd.bat.old"
    call :remove_legacy "ncd_service.bat.old"
    echo.
)

:: ---- Start the service --------------------------------------------------
echo Starting NCD Service...
pushd "%BIN_DIR%"
"%BIN_DIR%\NCDService.exe" start -log2
popd
timeout /t 2 /nobreak >nul

tasklist /FI "IMAGENAME eq NCDService.exe" 2>nul | "%SystemRoot%\System32\findstr.exe" /I /C:"NCDService.exe" >nul
if errorlevel 1 (
    echo   [WARN] Service may not have started. Check with: ncd_service status
) else (
    echo   [OK] Service started
)
echo.

:: ---- Summary ------------------------------------------------------------
echo Deployed version:
"%BIN_DIR%\NewChangeDirectory.exe" /v
echo.
echo ================================================
echo Deployment complete
echo ================================================
echo   Binaries : %BIN_DIR%
echo   PATH     : Machine PATH
echo   Data     : %DATA_DIR%
echo.
echo Open a new terminal, then verify with: ncd -v
echo.

endlocal
exit /b 0

:: ==========================================================================
:: Subroutines
:: ==========================================================================

:wait_for_service_stop
set "WAIT_COUNT=0"
:wait_loop
tasklist /FI "IMAGENAME eq NCDService.exe" 2>nul | "%SystemRoot%\System32\findstr.exe" /I /C:"NCDService.exe" >nul
if errorlevel 1 goto :eof
set /a WAIT_COUNT+=1
if %WAIT_COUNT% GEQ 10 (
    echo Warning: service did not stop gracefully; forcing termination...
    taskkill /F /IM NCDService.exe >nul 2>&1
    goto :eof
)
timeout /t 1 /nobreak >nul
goto wait_loop

:remove_legacy
if not exist "%DATA_DIR%\%~1" goto :eof
del /f /q "%DATA_DIR%\%~1" >nul 2>&1
if exist "%DATA_DIR%\%~1" (
    echo   [WARN] Could not remove legacy "%DATA_DIR%\%~1"
) else (
    echo   [OK] Removed legacy "%DATA_DIR%\%~1"
)
goto :eof
