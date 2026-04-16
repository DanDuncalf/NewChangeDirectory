@echo off
echo {"v":1,"drives":[{"letter":"U","count":7}]} > %TEMP%\findstr_test.txt
findstr /I "dirs" %TEMP%\findstr_test.txt
findstr /I "count" %TEMP%\findstr_test.txt
findstr /I "entries" %TEMP%\findstr_test.txt
echo exitcount=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
