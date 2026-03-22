@echo off
setlocal enabledelayedexpansion
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "LOG=%TEMP%\ncd_minimal_test3.log"
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_min_test3_%RANDOM%"
set "LOCALAPPDATA=%TEST_DATA%"
mkdir "%TEST_DATA%\NCD" 2>nul
echo. > "%TEST_DATA%\NCD\ncd.metadata"

echo [1] First: /rC with 10s timeout > "%LOG%"
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '/rC'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
echo [1] /rC done >> "%LOG%"

:: Check if database was created
dir "%TEST_DATA%\NCD\*.database" >> "%LOG%" 2>&1

echo [2] Now testing /r. >> "%LOG%"
mkdir "%TEMP%\ncd_min_tree3\testdir123\sub" 2>nul
pushd "%TEMP%\ncd_min_tree3"
"%NCD%" /r. >nul 2>&1
echo [2] /r. exit: %ERRORLEVEL% >> "%LOG%"
popd

echo [3] Search for testdir123 >> "%LOG%"
"%NCD%" testdir123 >nul 2>&1
echo [3] search exit: %ERRORLEVEL% >> "%LOG%"

echo [4] Done >> "%LOG%"
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "%TEMP%\ncd_min_tree3" 2>nul
type "%LOG%"
del "%LOG%" 2>nul
endlocal
