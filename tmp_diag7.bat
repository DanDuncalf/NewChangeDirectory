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
dir %TEST_DATA%\NCD\*.database
echo --- Agent check db-age ---
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /agent check --db-age --json
echo --- Agent check stats ---
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /agent check --stats --json
echo --- Agent query Reports ---
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /agent query Reports --json
echo --- Direct search stdout ---
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata Reports 2>&1
echo Exit: %ERRORLEVEL%
popd
rmdir /s /q %TEST_DATA% 2>nul
rmdir /s /q %TESTROOT% 2>nul
