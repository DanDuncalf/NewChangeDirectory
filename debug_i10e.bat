@echo off
setlocal

set "TEST_DATA=C:\Users\Dan\AppData\Local\Temp\ncd_i10_test"
set "DB_OVERRIDE=%TEST_DATA%\test.database"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""

echo === Batch direct args ===
echo %CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10

echo.
echo === PowerShell args string ===
powershell -NoProfile -Command "$args = '%CONF_OVERRIDE% /d ""%DB_OVERRIDE%"" L10'; Write-Host $args"

echo.
echo === Test with python via PowerShell ===
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = 'python'; $psi.Arguments = '-c """import sys; print(sys.argv)""" test_arg1'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(5000)"

echo.
echo === Test with python via PowerShell (complex args) ===
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = 'python'; $psi.Arguments = '-c """import sys; print(sys.argv)""" %CONF_OVERRIDE% /d ""%DB_OVERRIDE%"" L10'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(5000)"

endlocal
