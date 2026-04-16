@echo off
setlocal enabledelayedexpansion
set "TESTROOT=C:\Users\Dan\AppData\Local\Temp\ncd_test_wd"
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "CONF_OVERRIDE=-conf %TESTROOT%\ncd.metadata"
set "DB_OVERRIDE=%TESTROOT%\ncd_test.database"

rmdir /s /q "%TESTROOT%" 2>nul
mkdir "%TESTROOT%\subdir1\subdir2" 2>nul
mkdir "%TESTROOT%\NCD" 2>nul

:: Create minimal metadata
copy "%LOCALAPPDATA%\NCD\ncd.metadata" "%TESTROOT%\NCD\ncd.metadata" >nul 2>&1
if errorlevel 1 (
    echo Creating default metadata...
    echo. > "%TESTROOT%\NCD\ncd.metadata"
)

echo Test 1: Start-Process after pushd
cd /d E:\llama\NewChangeDirectory
pushd "%TESTROOT%"
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /d "%DB_OVERRIDE%"'; if (-not $p.WaitForExit(15000)) { $p.Kill() }"
popd

echo.
echo Checking database contents...
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent query subdir2 --json --limit 10

echo.
echo Current directory during test: %CD%
