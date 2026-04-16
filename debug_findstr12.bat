@echo off
echo {"v":1,"tree":[{\"n\":\"alpha\"}]} > %TEMP%\findstr_test.txt
findstr /C:"tree:" %TEMP%\findstr_test.txt
echo exit1=%ERRORLEVEL%
findstr /C:"\"tree\":" %TEMP%\findstr_test.txt
echo exit2=%ERRORLEVEL%
findstr "tree" %TEMP%\findstr_test.txt
echo exit3=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
