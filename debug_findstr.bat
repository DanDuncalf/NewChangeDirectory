@echo off
setlocal

echo {"v":1,"tree":[{"n":"alpha","d":0}]} > %TEMP%\fs_test.txt

echo === V2: findstr /L """n""" ===
findstr /L ""n"" %TEMP%\fs_test.txt >nul
echo Errorlevel: %ERRORLEVEL%

echo === V2: findstr /L /C:"""n""" ===
findstr /L /C:""n"" %TEMP%\fs_test.txt >nul
echo Errorlevel: %ERRORLEVEL%

echo === V2: findstr /I "alpha" ===
findstr /I "alpha" %TEMP%\fs_test.txt >nul
echo Errorlevel: %ERRORLEVEL%

echo === W7: findstr /L """n"":" ===
findstr /L ""n":"" %TEMP%\fs_test.txt >nul
echo Errorlevel: %ERRORLEVEL%

echo === W11: findstr /L "\"" ===
findstr /L "\"" %TEMP%\fs_test.txt >nul
echo Errorlevel: %ERRORLEVEL%

del %TEMP%\fs_test.txt 2>nul
endlocal
