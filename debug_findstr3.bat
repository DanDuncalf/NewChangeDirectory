@echo off
setlocal

echo {"v":1,"tree":[{"n":"alpha","d":0}]} > %TEMP%\fs_test.txt
echo projects\docs >> %TEMP%\fs_test.txt

echo === V2/W11: findstr "\\" ===
findstr "\\" %TEMP%\fs_test.txt >nul
echo Errorlevel: %ERRORLEVEL%

echo === V4: findstr /C:"\"v\"" ===
findstr /C:"\"v\"" %TEMP%\fs_test.txt >nul
echo Errorlevel: %ERRORLEVEL%

echo === V4: findstr /C:"\"d\"" ===
findstr /C:"\"d\"" %TEMP%\fs_test.txt >nul
echo Errorlevel: %ERRORLEVEL%

echo === W7: findstr /C:"\"n\"" ===
findstr /C:"\"n\"" %TEMP%\fs_test.txt >nul
echo Errorlevel: %ERRORLEVEL%

echo === W10: findstr /C:"\"tree\"" ===
findstr /C:"\"tree\"" %TEMP%\fs_test.txt >nul
echo Errorlevel: %ERRORLEVEL%

del %TEMP%\fs_test.txt 2>nul
endlocal
