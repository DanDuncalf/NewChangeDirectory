@echo off
setlocal
set NCD_TEST_MODE=1
set TEST_DATA=%TEMP%\ncd_diag_12345
mkdir %TEST_DATA%\NCD
set LOCALAPPDATA=%TEST_DATA%
set TESTROOT=%TEMP%\ncd_diag_tree_12345
mkdir %TESTROOT%\Reports
pushd %TESTROOT%
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /r. /d %TEST_DATA%\NCD\ncd_test.database
echo Rescan exit: %ERRORLEVEL%
dir %TEST_DATA%\NCD\ncd_test.database
echo --- Agent check stats ---
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /d %TEST_DATA%\NCD\ncd_test.database /agent check --stats
echo --- Agent query ---
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /d %TEST_DATA%\NCD\ncd_test.database /agent query Reports --json
popd
rmdir /s /q %TEST_DATA% 2>nul
rmdir /s /q %TESTROOT% 2>nul
