@echo off
echo xxv:1xx > %TEMP%\findstr_test.txt
cmd /c "findstr /L /C:\"v:1\" %TEMP%\findstr_test.txt"
echo exit1=%ERRORLEVEL%
cmd /c "findstr /L /C:\"v:\" %TEMP%\findstr_test.txt"
echo exit2=%ERRORLEVEL%
cmd /c "findstr /L /C:\"xxv:\" %TEMP%\findstr_test.txt"
echo exit3=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
