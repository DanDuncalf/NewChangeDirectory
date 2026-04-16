@echo off
setlocal
set NCD_TEST_MODE=1
set TEST_DATA=%TEMP%\ncd_diag_12345
mkdir %TEST_DATA%\NCD
set LOCALAPPDATA=%TEST_DATA%
set TESTROOT=%TEMP%\ncd_diag_tree_12345
mkdir %TESTROOT%\Reports
pushd %TESTROOT%
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /r. /d %TEST_DATA%\NCD\ncd_test.database >nul 2>&1
echo Rescan exit: %ERRORLEVEL%
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /d %TEST_DATA%\NCD\ncd_test.database Reports >nul 2>&1
echo Search exit: %ERRORLEVEL%
if exist %TEMP%\ncd_result.bat (
    type %TEMP%\ncd_result.bat
    call %TEMP%\ncd_result.bat
    echo NCD_STATUS=%NCD_STATUS%
) else (
    echo No result file
)
popd
rmdir /s /q %TEST_DATA% 2>nul
rmdir /s /q %TESTROOT% 2>nul
