@echo off
setlocal
set "TEST_PATH=C:\Users\Dan\AppData\Local\Temp\test.database"
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow -FilePath 'python' -ArgumentList '-c """import sys; print(sys.argv)""" -conf ""%TEST_PATH%"" /r.'; $p.WaitForExit(5000)"
endlocal
