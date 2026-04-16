@echo off
setlocal
set "ARG=-conf \"C:\test path\" /r. /d \"C:\test db\""
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow -Wait -FilePath 'E:\llama\NewChangeDirectory\NewChangeDirectory.exe' -ArgumentList '%ARG%'; Write-Host ('Exit code: ' + $p.ExitCode)"
