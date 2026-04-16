@echo off
echo alpha\src > %TEMP%\findstr_test.txt
echo alpha\docs >> %TEMP%\findstr_test.txt
findstr "\"" %TEMP%\findstr_test.txt
echo exit1=%ERRORLEVEL%
findstr "\\" %TEMP%\findstr_test.txt
echo exit2=%ERRORLEVEL%
findstr "src" %TEMP%\findstr_test.txt
echo exit3=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
