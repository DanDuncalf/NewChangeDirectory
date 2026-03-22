@echo off
setlocal enabledelayedexpansion

set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "LOG=%TEMP%\ncd_minimal_test.log"
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_min_test_%RANDOM%"
set "LOCALAPPDATA=%TEST_DATA%"

mkdir "%TEST_DATA%\NCD" 2>nul
echo. > "%TEST_DATA%\NCD\ncd.metadata"
mkdir "%TEMP%\ncd_min_tree\alpha\beta" 2>nul

echo [1] Setup done > "%LOG%"

echo [2] Testing /h... >> "%LOG%"
"%NCD%" /h >nul 2>&1
echo [2] /h exit: %ERRORLEVEL% >> "%LOG%"

echo [3] Testing /v... >> "%LOG%"
"%NCD%" /v >nul 2>&1
echo [3] /v exit: %ERRORLEVEL% >> "%LOG%"

echo [4] Testing /r. ... >> "%LOG%"
pushd "%TEMP%\ncd_min_tree"
"%NCD%" /r. >nul 2>&1
echo [4] /r. exit: %ERRORLEVEL% >> "%LOG%"
popd

echo [5] Testing search... >> "%LOG%"
"%NCD%" alpha >nul 2>&1
echo [5] search exit: %ERRORLEVEL% >> "%LOG%"

echo [6] All done >> "%LOG%"

:: Cleanup
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "%TEMP%\ncd_min_tree" 2>nul

type "%LOG%"
del "%LOG%" 2>nul
endlocal
