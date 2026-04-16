@echo off
echo {"v":1,"db_age":0} > %TEMP%\findstr_test.txt
findstr /I "age" %TEMP%\findstr_test.txt
echo exit_age=%ERRORLEVEL%
findstr /R "age" %TEMP%\findstr_test.txt
echo exit_rage=%ERRORLEVEL%
findstr /R "age\|hours" %TEMP%\findstr_test.txt
echo exit_or=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
