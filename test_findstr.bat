@echo off
echo alpha\src > %TEMP%\findstr_test.txt
findstr "\"" %TEMP%\findstr_test.txt
echo Exit code: %ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
