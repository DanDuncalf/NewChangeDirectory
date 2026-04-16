@echo off
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow cmd -ArgumentList '/c exit 42'; [System.Threading.Thread]::Sleep(500); Write-Host ('HasExited=' + $p.HasExited); Write-Host ('ExitCode=' + $p.ExitCode); if ($p.ExitCode -eq $null) { Write-Host 'ExitCode is NULL' } else { Write-Host ('ExitCode value=' + $p.ExitCode) }"
