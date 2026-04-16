@echo off
echo alpha > %TEMP%\findstr_test.txt
echo   docs >> %TEMP%\findstr_test.txt
echo   src >> %TEMP%\findstr_test.txt
findstr /B "  " %TEMP%\findstr_test.txt
echo exit1=%ERRORLEVEL%
findstr /B "docs" %TEMP%\findstr_test.txt
echo exit2=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
