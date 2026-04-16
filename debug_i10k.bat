@echo off
setlocal

set "TEST_DATA=C:\Users\Dan\AppData\Local\Temp\ncd_i10_test"
set "DB_OVERRIDE=%TEST_DATA%\test.database"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""

echo === Batch direct argv ===
python -c "import sys; print(sys.argv)" %CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%"

echo.
echo === PowerShell StartProcess argv ===
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = 'python'; $psi.Arguments = '%CONF_OVERRIDE% /r. /i /s /d ""%DB_OVERRIDE%""'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(5000)"

echo.
echo === PowerShell StartProcess argv (test method) ===
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = 'python'; $psi.Arguments = '%CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%"'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(5000)"

endlocal
