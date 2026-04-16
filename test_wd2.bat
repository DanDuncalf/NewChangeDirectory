@echo off
setlocal
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"

echo Current dir before pushd: %CD%
pushd C:\Users\Dan
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = 'cmd'; $psi.Arguments = '/c cd'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $null = $p.WaitForExit(5000)"
popd
echo Current dir after popd: %CD%
