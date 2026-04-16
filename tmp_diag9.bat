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
echo --- Database drive label ---
powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('%TEST_DATA%\NCD\ncd_C.database'); $label=[System.Text.Encoding]::ASCII.GetString($b, 10, 64).Trim([char]0); Write-Host 'Label: ['$label']'"
echo --- Search ---
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata Reports
echo Search exit: %ERRORLEVEL%
popd
rmdir /s /q %TEST_DATA% 2>nul
rmdir /s /q %TESTROOT% 2>nul
