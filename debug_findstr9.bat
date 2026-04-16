@echo off
echo {\"v\":1} > %TEMP%\findstr_test.txt
cmd /c "findstr '"v":1' %TEMP%\findstr_test.txt"
echo exit1=%ERRORLEVEL%
cmd /c "findstr /L /C:"""v"":1" %TEMP%\findstr_test.txt"
echo exit2=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
