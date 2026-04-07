@echo off
setlocal enabledelayedexpansion
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "LOG=%TEMP%\ncd_minimal_test4.log"
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_min_test4_%RANDOM%"
set "LOCALAPPDATA=%TEST_DATA%"

:: Disable NCD background rescans to prevent scanning user drives during tests
set "NCD_TEST_MODE=1"

mkdir "%TEST_DATA%\NCD" 2>nul

:: Create test tree in TEMP (not using E: drive directly)
set "TEST_TREE=%TEMP%\ncd_min_tree4"
mkdir "%TEST_TREE%\testdir456\sub" 2>nul

:: Copy user's existing database and metadata (for testing with existing data)
echo [1] Copying existing database... > "%LOG%"
copy "%REAL_LOCALAPPDATA%\NCD\*.database" "%TEST_DATA%\NCD\" >> "%LOG%" 2>&1
copy "%REAL_LOCALAPPDATA%\NCD\ncd.metadata" "%TEST_DATA%\NCD\" >> "%LOG%" 2>&1

:: CRITICAL: Disable auto-rescan in copied metadata to prevent full system scan
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" /c rescan_interval_hours=-1 >nul 2>&1

:: Now try /r. to add our test dirs to the existing database
echo [2] Testing /r. with existing DB >> "%LOG%"
pushd "%TEST_TREE%"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" /r. >nul 2>&1
echo [2] /r. exit: %ERRORLEVEL% >> "%LOG%"
popd

:: Search for a known dir and our test dir
echo [3] Search Downloads >> "%LOG%"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" Downloads >nul 2>&1
echo [3] search exit: %ERRORLEVEL% >> "%LOG%"

echo [4] Search testdir456 >> "%LOG%"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" testdir456 >nul 2>&1
echo [4] search exit: %ERRORLEVEL% >> "%LOG%"

echo [5] Done >> "%LOG%"
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "%TEST_TREE%" 2>nul
type "%LOG%"
del "%LOG%" 2>nul
endlocal
