@echo off
setlocal
set "NCD_TEST_MODE=1"
set "LOCALAPPDATA=%TEMP%\ncd_debug_test3"
mkdir "%LOCALAPPDATA%\NCD" 2>nul
set "NCD=%CD%\NewChangeDirectory.exe"
set "TESTROOT=%TEMP%\ncd_debug_tree3"
mkdir "%TESTROOT%\Special Chars\dir with spaces" 2>nul

powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%LOCALAPPDATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"
set "CONF_OVERRIDE=-conf "%LOCALAPPDATA%\NCD\ncd.metadata""
set "DB_OVERRIDE=%LOCALAPPDATA%\NCD\ncd_test.database"

pushd "%TESTROOT%"
"%NCD%" %CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%"
popd

echo === Single arg ===
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d "%DB_OVERRIDE%" \"dir with spaces\"'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(5000) | Out-Null"
if exist "%TEMP%\ncd_result.bat" (
    type "%TEMP%\ncd_result.bat"
    del "%TEMP%\ncd_result.bat"
)

echo.
echo === Double arg ===
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d "%DB_OVERRIDE%" \"dir with spaces\" \"dir with spaces\"'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(5000) | Out-Null"
if exist "%TEMP%\ncd_result.bat" (
    type "%TEMP%\ncd_result.bat"
    del "%TEMP%\ncd_result.bat"
)

rmdir /s /q "%LOCALAPPDATA%" 2>nul
rmdir /s /q "%TESTROOT%" 2>nul
endlocal
