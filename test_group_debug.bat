@echo off
setlocal EnableDelayedExpansion
set "TESTROOT=%TEMP%\ncd_grp_dbg_%RANDOM%%RANDOM%"
mkdir "%TESTROOT%\Projects\alpha" 2>nul
mkdir "%TESTROOT%\Users\scott" 2>nul
mkdir "%TESTROOT%\Media" 2>nul
set "TEST_DATA=%TEMP%\ncd_grp_data_%RANDOM%%RANDOM%"
mkdir "%TEST_DATA%\NCD" 2>nul
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "CONF=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "DB=-d "%TEST_DATA%\NCD\ncd_test.database""
set NCD_TEST_MODE=1

"%NCD%" %CONF% %DB% /r. "%TESTROOT%"
echo Scan exit: %ERRORLEVEL%

pushd "%TESTROOT%\Projects"
"%NCD%" %CONF% /g @proj
echo Create @proj exit: %ERRORLEVEL%
popd

pushd "%TESTROOT%\Users"
"%NCD%" %CONF% /g @users
echo Create @users exit: %ERRORLEVEL%
popd

"%NCD%" %CONF% %DB% @proj
echo Navigate @proj exit: %ERRORLEVEL%

"%NCD%" %CONF% /gl
echo List groups exit: %ERRORLEVEL%

rmdir /s /q "%TESTROOT%" 2>nul
rmdir /s /q "%TEST_DATA%" 2>nul
