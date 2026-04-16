@echo off
echo xxc:1xx > %TEMP%\findstr_test.txt
cmd /c "findstr /L /C:\"c:1\" %TEMP%\findstr_test.txt"
echo exit1=%ERRORLEVEL%
cmd /c "findstr /L /C:\"x:1\" %TEMP%\findstr_test.txt"
echo exit2=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
