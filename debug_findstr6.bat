@echo off
echo {"v":1,"tree":[{"n":"alpha"}]} > %TEMP%\findstr_test.txt
echo v:> %TEMP%\findstr_pat.txt
cmd /c "findstr /L /G:%TEMP%\findstr_pat.txt %TEMP%\findstr_test.txt"
echo exit_g=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
del %TEMP%\findstr_pat.txt 2>nul
