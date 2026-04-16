@echo off
echo {"v":1,"tree":[{"n":"alpha"}]} > %TEMP%\findstr_test.txt
cmd /c "findstr /L /C:\"v\" %TEMP%\findstr_test.txt"
echo exit_v=%ERRORLEVEL%
cmd /c "findstr /L /C:\":\" %TEMP%\findstr_test.txt"
echo exit_colon=%ERRORLEVEL%
cmd /c "findstr /L /C:\"v:\" %TEMP%\findstr_test.txt"
echo exit_vcol=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
