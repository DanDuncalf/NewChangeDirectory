@echo off
echo {"v":1,"tree":[{"n":"alpha"}]} > %TEMP%\findstr_test.txt
findstr /L '"v":1' %TEMP%\findstr_test.txt
echo exit1=%ERRORLEVEL%
findstr /L /C:'"v":1' %TEMP%\findstr_test.txt
echo exit2=%ERRORLEVEL%
findstr /L "v" %TEMP%\findstr_test.txt
echo exit3=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
