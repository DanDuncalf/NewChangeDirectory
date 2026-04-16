@echo off
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow -FilePath 'E:\llama\NewChangeDirectory\NewChangeDirectory.exe' -ArgumentList '/?'; $p.WaitForExit(); Write-Host ('ExitCode=' + $p.ExitCode)"
