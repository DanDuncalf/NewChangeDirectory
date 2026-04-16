@echo off
setlocal EnableDelayedExpansion
set "TESTROOT=%TEMP%\ncd_dbg2_%RANDOM%%RANDOM%"
mkdir "%TESTROOT%\Users\scott\.hidden_config" 2>nul
attrib +h "%TESTROOT%\Users\scott\.hidden_config" 2>nul
mkdir "%TESTROOT%\WinSys\System32\drivers\etc" 2>nul
attrib +s "%TESTROOT%\WinSys" 2>nul
attrib +s "%TESTROOT%\WinSys\System32" 2>nul
mkdir "%TESTROOT%\Special Chars\dir with spaces" 2>nul
mkdir "%TESTROOT%\Projects\alpha\src\main" 2>nul
mkdir "%TESTROOT%\Deep\L1\L2\L3\L4\L5\L6\L7\L8\L9\L10" 2>nul
set "TEST_DATA=%TEMP%\ncd_dbg2_data_%RANDOM%%RANDOM%"
mkdir "%TEST_DATA%\NCD" 2>nul
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "CONF=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "DB=-d "%TEST_DATA%\NCD\ncd_test.database""
set NCD_TEST_MODE=1

pushd "%TESTROOT%"
"%NCD%" %CONF% %DB% /r. /i /s
echo Scan exit: %ERRORLEVEL%
popd

"%NCD%" %CONF% %DB% /i .hidden_config
echo hidden_config exit: %ERRORLEVEL%

"%NCD%" %CONF% %DB% /s System32
echo System32 exit: %ERRORLEVEL%

"%NCD%" %CONF% %DB% "dir with spaces"
echo spaces exit: %ERRORLEVEL%

"%NCD%" %CONF% %DB% /z /i .hidden
echo fuzzy hidden exit: %ERRORLEVEL%

rmdir /s /q "%TESTROOT%" 2>nul
rmdir /s /q "%TEST_DATA%" 2>nul
