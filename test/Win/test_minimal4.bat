@echo off
setlocal enabledelayedexpansion
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "LOG=%TEMP%\ncd_minimal_test4.log"
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_min_test4_%RANDOM%"
set "LOCALAPPDATA=%TEST_DATA%"
mkdir "%TEST_DATA%\NCD" 2>nul

:: Copy user's existing database and metadata
echo [1] Copying existing database... > "%LOG%"
copy "%REAL_LOCALAPPDATA%\NCD\*.database" "%TEST_DATA%\NCD\" >> "%LOG%" 2>&1
copy "%REAL_LOCALAPPDATA%\NCD\ncd.metadata" "%TEST_DATA%\NCD\" >> "%LOG%" 2>&1

:: Create test tree
mkdir "%TEMP%\ncd_min_tree4\testdir456\sub" 2>nul

:: Now try /r. to add our test dirs to the existing database
echo [2] Testing /r. with existing DB >> "%LOG%"
pushd "%TEMP%\ncd_min_tree4"
"%NCD%" /r. >nul 2>&1
echo [2] /r. exit: %ERRORLEVEL% >> "%LOG%"
popd

:: Search for a known dir and our test dir
echo [3] Search Downloads >> "%LOG%"
"%NCD%" Downloads >nul 2>&1
echo [3] search exit: %ERRORLEVEL% >> "%LOG%"

echo [4] Search testdir456 >> "%LOG%"
"%NCD%" testdir456 >nul 2>&1
echo [4] search exit: %ERRORLEVEL% >> "%LOG%"

echo [5] Done >> "%LOG%"
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "%TEMP%\ncd_min_tree4" 2>nul
type "%LOG%"
del "%LOG%" 2>nul
endlocal
