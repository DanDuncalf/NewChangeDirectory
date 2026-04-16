@echo off
echo {"v":1,"tree":[{"n":"alpha","d":0}]} > %TEMP%\findstr_test.txt
findstr /C:"\"v\"" %TEMP%\findstr_test.txt
echo exit_v=%ERRORLEVEL%
findstr /C:"\"tree\"" %TEMP%\findstr_test.txt
echo exit_tree=%ERRORLEVEL%
findstr /C:"\"n\"" %TEMP%\findstr_test.txt
echo exit_n=%ERRORLEVEL%
findstr /C:"\"d\"" %TEMP%\findstr_test.txt
echo exit_d=%ERRORLEVEL%
del %TEMP%\findstr_test.txt 2>nul
