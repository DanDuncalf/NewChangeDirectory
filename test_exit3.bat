@echo off
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow -FilePath 'cmd' -ArgumentList '/c exit 42'; $p.WaitForExit(); Write-Host ('Type=' + $p.GetType().FullName); Write-Host ('HasExited=' + $p.HasExited); Write-Host ('ExitCode=' + $p.ExitCode)"
