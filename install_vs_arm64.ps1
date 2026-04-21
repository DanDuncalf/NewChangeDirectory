$installer = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vs_installer.exe"
$installPath = "C:\Program Files\Microsoft Visual Studio\18\Community"

if (-not (Test-Path $installer)) {
    Write-Error "VS Installer not found at: $installer"
    exit 1
}

Write-Host "Installing ARM64 build tools..."
& $installer modify `
    --installPath $installPath `
    --add Microsoft.VisualStudio.Component.VC.ARM64 `
    --quiet --wait --norestart

$exitCode = $LASTEXITCODE
Write-Host "Exit code: $exitCode"
exit $exitCode
