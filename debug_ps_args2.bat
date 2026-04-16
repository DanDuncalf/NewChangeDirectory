@echo off
setlocal
set "TEST_PATH=C:\Users\Dan\AppData\Local\Temp\test.database"
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow -FilePath 'E:\llama\NewChangeDirectory\NewChangeDirectory.exe' -ArgumentList '-conf ""%TEST_PATH%"" /agent check --stats'; $p.WaitForExit(5000)"
endlocal
