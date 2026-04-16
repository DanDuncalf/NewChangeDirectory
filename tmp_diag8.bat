@echo off
setlocal
set NCD_TEST_MODE=1
set TEST_DATA=%TEMP%\ncd_diag_12345
rmdir /s /q %TEST_DATA% 2>nul
mkdir %TEST_DATA%\NCD
set LOCALAPPDATA=%TEST_DATA%
set TESTROOT=%TEMP%\ncd_diag_tree_12345
rmdir /s /q %TESTROOT% 2>nul
mkdir %TESTROOT%\Reports
mkdir %TESTROOT%\Reports\Sub1
pushd %TESTROOT%
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /r. >nul 2>&1
echo Rescan exit: %ERRORLEVEL%
echo --- Database hex dump (first 64 bytes) ---
powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('%TEST_DATA%\NCD\ncd_C.database'); for($i=0; $i -lt [Math]::Min(64,$b.Length); $i++) { Write-Host -NoNewline ('{0:X2} ' -f $b[$i]); if(($i+1)%16 -eq 0) { Write-Host } }"
echo --- Search with stdout ---
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata Reports
echo Search exit: %ERRORLEVEL%
popd
rmdir /s /q %TEST_DATA% 2>nul
rmdir /s /q %TESTROOT% 2>nul
