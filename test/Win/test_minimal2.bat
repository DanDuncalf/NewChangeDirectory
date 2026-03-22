@echo off
setlocal enabledelayedexpansion
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "LOG=%TEMP%\ncd_minimal_test2.log"
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_min_test2_%RANDOM%"
set "LOCALAPPDATA=%TEST_DATA%"
mkdir "%TEST_DATA%\NCD" 2>nul
echo. > "%TEST_DATA%\NCD\ncd.metadata"
mkdir "E:\llama\ncd_test_scan\a\b\c" 2>nul

echo [1] LOCALAPPDATA=%LOCALAPPDATA% > "%LOG%"
echo [2] Test dir: E:\llama\ncd_test_scan >> "%LOG%"

pushd "E:\llama\ncd_test_scan"
echo [3] CWD: %CD% >> "%LOG%"
"%NCD%" /r. >nul 2>&1
echo [3] /r. exit: %ERRORLEVEL% >> "%LOG%"
popd

echo [4] Now searching... >> "%LOG%"
"%NCD%" ncd_test_scan >nul 2>&1
echo [4] search exit: %ERRORLEVEL% >> "%LOG%"

echo [5] Done >> "%LOG%"
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "E:\llama\ncd_test_scan" 2>nul
type "%LOG%"
del "%LOG%" 2>nul
endlocal
