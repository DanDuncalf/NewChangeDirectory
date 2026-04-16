@echo off
echo {\"v\": 1,\"tree\":[{\"n\":\"alpha\"}]} > %TEMP%\findstr_test.txt
cmd /c "findstr /L /C:\"v:\" %TEMP%\findstr_test.txt"
echo exit1=%ERRORLEVEL%
cmd /c "findstr /L /C:\"v\":\" %TEMP%\findstr_test.txt"
echo exit2=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
