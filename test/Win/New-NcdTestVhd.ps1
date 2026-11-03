# New-NcdTestVhd.ps1 -- create, format and mount a test VHD without ever
# exposing an unformatted volume.
#
# Why: diskpart's "create partition primary" lets Windows auto-mount the new,
# still-RAW partition at the next free letter, and Explorer then pops up
# "You need to format the disk in drive F: before you can use it". Here the
# partition is created with no drive letter, formatted, and only then given
# the requested letter.
#
# Usage (elevated):
#   powershell -NoProfile -ExecutionPolicy Bypass -File New-NcdTestVhd.ps1 `
#       -Path C:\Temp\x.vhdx -Letter Z -SizeMB 50 -Label NCDTest
# Exit code 0 on success; the drive is then available at <Letter>:\.
# Detach with: diskpart "select vdisk file=..." / "detach vdisk", or
# Dismount-DiskImage -ImagePath <Path>.

param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z]$')][string]$Letter,
    [int]$SizeMB = 50,
    [string]$Label = "NCDTest"
)

$ErrorActionPreference = "Stop"
$Letter = $Letter.ToUpper()

try {
    if (Test-Path $Path) { Remove-Item $Path -Force }

    # 1. Create and attach an empty (uninitialised) virtual disk. With no
    #    partition there is no volume, so nothing can be auto-mounted.
    $dp = [IO.Path]::GetTempFileName()
    try {
        @(
            "create vdisk file=`"$Path`" maximum=$SizeMB type=expandable",
            "select vdisk file=`"$Path`"",
            "attach vdisk"
        ) | Out-File -FilePath $dp -Encoding ascii
        $out = & diskpart /s $dp 2>&1
        if ($LASTEXITCODE -ne 0) { throw "diskpart create/attach failed: $($out -join ' ')" }
    } finally {
        Remove-Item $dp -ErrorAction SilentlyContinue
    }

    $disk = $null
    for ($i = 0; $i -lt 20 -and -not $disk; $i++) {
        $disk = Get-DiskImage -ImagePath $Path | Get-Disk -ErrorAction SilentlyContinue
        if (-not $disk) { Start-Sleep -Milliseconds 250 }
    }
    if (-not $disk) { throw "attached disk for $Path not found" }

    # 2. Partition WITHOUT a drive letter, 3. format, 4. assign the letter.
    Initialize-Disk -Number $disk.Number -PartitionStyle MBR
    $part = New-Partition -DiskNumber $disk.Number -UseMaximumSize
    $part | Format-Volume -FileSystem NTFS -NewFileSystemLabel $Label -Confirm:$false | Out-Null
    Set-Partition -DiskNumber $disk.Number -PartitionNumber $part.PartitionNumber -NewDriveLetter $Letter

    for ($i = 0; $i -lt 20 -and -not (Test-Path "${Letter}:\"); $i++) { Start-Sleep -Milliseconds 250 }
    if (-not (Test-Path "${Letter}:\")) { throw "${Letter}: not available after mounting" }
    Write-Host "[TEST_DRIVE] VHD $Path mounted at ${Letter}:\"
    exit 0
} catch {
    Write-Host "[TEST_DRIVE] New-NcdTestVhd failed: $($_.Exception.Message)"
    try { Dismount-DiskImage -ImagePath $Path -ErrorAction SilentlyContinue | Out-Null } catch { }
    Remove-Item $Path -Force -ErrorAction SilentlyContinue
    exit 1
}
