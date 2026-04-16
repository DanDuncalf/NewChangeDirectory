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
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /r. /d %TEST_DATA%\NCD\ncd_test.database >nul 2>&1
echo Rescan exit: %ERRORLEVEL%
dir %TEST_DATA%\NCD\ncd_test.database
echo File size:
for %%F in (%TEST_DATA%\NCD\ncd_test.database) do echo %%~zF
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /d %TEST_DATA%\NCD\ncd_test.database /v >nul 2>&1
echo Version exit: %ERRORLEVEL%
popd
rmdir /s /q %TEST_DATA% 2>nul
rmdir /s /q %TESTROOT% 2>nul
