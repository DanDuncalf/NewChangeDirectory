@echo off
setlocal enabledelayedexpansion
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "LOG=%TEMP%\ncd_minimal_test3.log"
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_min_test3_%RANDOM%"
set "LOCALAPPDATA=%TEST_DATA%"
mkdir "%TEST_DATA%\NCD" 2>nul
echo. > "%TEST_DATA%\NCD\ncd.metadata"

:: CRITICAL: Disable auto-rescan to prevent background scans of user drives
set "NCD_TEST_MODE=1"

:: Create test tree in temp (not on E: drive directly)
mkdir "%TEMP%\ncd_min_tree3\testdir123\sub" 2>nul

echo [1] Creating test database with /r. (subdirectory scan only) > "%LOG%"
pushd "%TEMP%\ncd_min_tree3"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" /r. >nul 2>&1
echo [1] /r. exit: %ERRORLEVEL% >> "%LOG%"
popd

:: Check if database was created
dir "%TEST_DATA%\NCD\*.database" >> "%LOG%" 2>&1

echo [2] Search for testdir123 >> "%LOG%"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" testdir123 >nul 2>&1
echo [2] search exit: %ERRORLEVEL% >> "%LOG%"

echo [3] Done >> "%LOG%"

:: Cleanup - restore LOCALAPPDATA
call :cleanup
type "%LOG%"
del "%LOG%" 2>nul
endlocal
exit /b 0

:cleanup
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
if exist "%TEST_DATA%" rmdir /s /q "%TEST_DATA%" 2>nul
if exist "%TEMP%\ncd_min_tree3" rmdir /s /q "%TEMP%\ncd_min_tree3" 2>nul
goto :eof
