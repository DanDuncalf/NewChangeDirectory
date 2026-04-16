@echo off
setlocal
set NCD_TEST_MODE=1
set TEST_DATA=%TEMP%\ncd_diag_12345
mkdir %TEST_DATA%\NCD
set LOCALAPPDATA=%TEST_DATA%
set TESTROOT=%TEMP%\ncd_diag_tree_12345
mkdir %TESTROOT%\Reports
mkdir %TESTROOT%\Reports\Sub1
pushd %TESTROOT%
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /r. /d %TEST_DATA%\NCD\ncd_test.database > %TEMP%\diag_rescan.txt 2>&1
type %TEMP%\diag_rescan.txt
echo ---
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /d %TEST_DATA%\NCD\ncd_test.database /agent tree %TESTROOT% --depth 3 > %TEMP%\diag_tree.txt 2>&1
type %TEMP%\diag_tree.txt
echo ---
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /d %TEST_DATA%\NCD\ncd_test.database /agent query Reports --json > %TEMP%\diag_query.txt 2>&1
type %TEMP%\diag_query.txt
popd
rmdir /s /q %TEST_DATA% 2>nul
rmdir /s /q %TESTROOT% 2>nul
