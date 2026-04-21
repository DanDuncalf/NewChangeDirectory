$installer = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vs_installer.exe"
$installPath = "C:\Program Files\Microsoft Visual Studio\18\Community"

if (-not (Test-Path $installer)) {
    Write-Error "VS Installer not found"
    exit 1
}

Write-Host "Installing NativeDesktop workload (this may take a while)..."
& $installer modify `
    --installPath $installPath `
    --add Microsoft.VisualStudio.Workload.NativeDesktop `
    --quiet --wait --norestart

Write-Host "Exit code: $LASTEXITCODE"
exit $LASTEXITCODE
