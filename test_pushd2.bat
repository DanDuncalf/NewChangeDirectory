@echo off
pushd C:\Users\Dan
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow -FilePath 'powershell' -ArgumentList '-NoProfile', '-Command', 'Write-Host (Get-Location)'; $p.WaitForExit(5000)"
popd
