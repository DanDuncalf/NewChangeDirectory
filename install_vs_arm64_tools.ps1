$installer = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vs_installer.exe"
$installPath = "C:\Program Files\Microsoft Visual Studio\18\Community"

if (-not (Test-Path $installer)) {
    Write-Error "VS Installer not found"
    exit 1
}

Write-Host "Installing VC.Tools.ARM64..."
& $installer modify `
    --installPath $installPath `
    --add Microsoft.VisualStudio.Component.VC.Tools.ARM64 `
    --quiet --wait --norestart

Write-Host "Exit code: $LASTEXITCODE"

# Also try ARM64EC
Write-Host "Installing VC.ARM64EC..."
& $installer modify `
    --installPath $installPath `
    --add Microsoft.VisualStudio.Component.VC.ARM64EC `
    --quiet --wait --norestart

Write-Host "Exit code: $LASTEXITCODE"
exit 0
