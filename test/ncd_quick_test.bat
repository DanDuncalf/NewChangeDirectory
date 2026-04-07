@echo off
setlocal enabledelayedexpansion

:: Save original LOCALAPPDATA
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"

:: Create isolated test location
set "TEST_DATA=%TEMP%\ncd_quick_test_%RANDOM%"
mkdir "%TEST_DATA%\NCD" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"

:: CRITICAL: Disable auto-rescan to prevent background scans of user drives
set "NCD_TEST_MODE=1"

:: Create test tree
mkdir "%TEMP%\ncd_quick_test\a\b\c" 2>nul
cd /d "%TEMP%\ncd_quick_test"
echo CWD: %CD%
echo Running NCD /r. ...

:: Run NCD with isolated config
E:\llama\NewChangeDirectory\NewChangeDirectory.exe -conf "%TEST_DATA%\NCD\ncd.metadata" /r.
echo EXIT: %ERRORLEVEL%
echo DONE

:: Cleanup - restore LOCALAPPDATA and remove test data
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
if exist "%TEST_DATA%" rmdir /s /q "%TEST_DATA%" 2>nul

endlocal
