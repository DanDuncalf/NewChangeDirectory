@echo off
setlocal
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_test_group_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"

set "TESTROOT=%TEMP%\ncd_test_tree_%RANDOM%"
mkdir "%TESTROOT%\Projects" 2>nul

set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""

mkdir "%TEST_DATA%\NCD" 2>nul
copy "%REAL_LOCALAPPDATA%\NCD\ncd.metadata" "%TEST_DATA%\NCD\ncd.metadata" >nul 2>&1
if errorlevel 1 powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"

set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1

echo === Creating group with direct call ===
cd /d "%TESTROOT%\Projects"
"%NCD%" %CONF_OVERRIDE% /g @proj

echo.
echo === Checking group ===
"%NCD%" %CONF_OVERRIDE% @proj

echo.
echo === Creating group with Start-Process (old) ===
cd /d "%TESTROOT%\Projects"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /g @proj2'; if (-not $p.WaitForExit(10000)) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
echo Old Start-Process ERRORLEVEL: %ERRORLEVEL%

echo.
echo === Creating group with System.Diagnostics.Process (new) ===
cd /d "%TESTROOT%\Projects"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /g @proj3'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(10000); if (-not $ok) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
echo New Process ERRORLEVEL: %ERRORLEVEL%

echo.
echo === Checking @proj2 ===
"%NCD%" %CONF_OVERRIDE% @proj2
echo.
echo === Checking @proj3 ===
"%NCD%" %CONF_OVERRIDE% @proj3

:: Cleanup
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "%TESTROOT%" 2>nul
