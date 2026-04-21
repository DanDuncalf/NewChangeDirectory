#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Master packaging script for NCD cross-platform builds.
.DESCRIPTION
    Builds NCD for Windows (x64, ARM64) and Linux (x64, ARM64, RISC-V)
    and creates ZIP/tar.gz installation packages.
#>

[CmdletBinding()]
param(
    [switch]$SkipWindowsBuild,
    [switch]$SkipLinuxBuild,
    [switch]$SkipWindowsArm64,
    [switch]$SkipLinuxArm64,
    [switch]$SkipLinuxRiscv,
    [string]$Version = "1.3.0"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$DistDir = Join-Path $ProjectRoot "dist"
$PackagingDir = Join-Path $ProjectRoot "packaging"
$SharedDir = Join-Path $ProjectRoot ".." "shared"

# ========================================================================
# Helper Functions
# ========================================================================

function Write-Header($text) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host $text -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
}

function Test-Command($cmd) {
    $null -ne (Get-Command $cmd -ErrorAction SilentlyContinue)
}

function Ensure-Directory($path) {
    if (-not (Test-Path $path)) {
        New-Item -ItemType Directory -Path $path -Force | Out-Null
    }
}

function Invoke-WithVcVars($arch, $scriptBlock) {
    $vcvarsPath = $null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($installPath) {
            if ($arch -eq "arm64") {
                $vcvarsPath = Join-Path $installPath "VC\Auxiliary\Build\vcvarsarm64.bat"
                if (-not (Test-Path $vcvarsPath)) {
                    # Try vcvarsall with arm64 argument
                    $vcvarsall = Join-Path $installPath "VC\Auxiliary\Build\vcvarsall.bat"
                    if (Test-Path $vcvarsall) {
                        $vcvarsPath = $vcvarsall
                    }
                }
            } else {
                $vcvarsPath = Join-Path $installPath "VC\Auxiliary\Build\vcvars64.bat"
            }
        }
    }

    if (-not $vcvarsPath -or -not (Test-Path $vcvarsPath)) {
        throw "Could not find Visual Studio vcvars script for $arch"
    }

    Write-Host "Using VC vars: $vcvarsPath"

    if ($vcvarsPath -match "vcvarsall\.bat") {
        cmd /c "`"$vcvarsPath`" arm64 && powershell -Command `"& { $scriptBlock }`""
    } else {
        cmd /c "`"$vcvarsPath`" && powershell -Command `"& { $scriptBlock }`""
    }
}

# ========================================================================
# Pre-flight Checks
# ========================================================================

Write-Header "NCD Cross-Platform Packaging"

Write-Host "Project root: $ProjectRoot"
Write-Host "Dist dir: $DistDir"
Write-Host "Version: $Version"
Write-Host ""

if (-not (Test-Path $SharedDir)) {
    Write-Error "Shared library not found at: $SharedDir"
    exit 1
}

Ensure-Directory $DistDir

# ========================================================================
# Phase 1: Build Windows x64
# ========================================================================

if (-not $SkipWindowsBuild) {
    Write-Header "Building Windows x64"

    if (-not (Test-Path (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"))) {
        Write-Warning "Visual Studio not found. Skipping Windows x64 build."
    } else {
        & cmd /c "`"$ProjectRoot\build.bat`" x64"
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Windows x64 build failed"
            exit 1
        }

        $zipName = "NCD-$Version-windows-x64.zip"
        $zipPath = Join-Path $DistDir $zipName
        $tempDir = Join-Path $env:TEMP "ncd_pkg_win_x64_$([Guid]::NewGuid().ToString('N').Substring(0,8))"
        Ensure-Directory $tempDir

        Copy-Item (Join-Path $ProjectRoot "NewChangeDirectory.exe") $tempDir
        Copy-Item (Join-Path $ProjectRoot "NCDService.exe") $tempDir
        Copy-Item (Join-Path $ProjectRoot "ncd.bat") $tempDir
        Copy-Item (Join-Path $ProjectRoot "ncd_service.bat") $tempDir
        Copy-Item (Join-Path $PackagingDir "windows\install.bat") $tempDir
        Copy-Item (Join-Path $ProjectRoot "README.md") $tempDir

        if (Test-Path (Join-Path $ProjectRoot "completions")) {
            Copy-Item -Recurse (Join-Path $ProjectRoot "completions") $tempDir
        }

        Compress-Archive -Path "$tempDir\*" -DestinationPath $zipPath -Force
        Remove-Item -Recurse -Force $tempDir

        Write-Host "Created: $zipPath" -ForegroundColor Green
    }
}

# ========================================================================
# Phase 2: Build Windows ARM64
# ========================================================================

if (-not $SkipWindowsBuild -and -not $SkipWindowsArm64) {
    Write-Header "Building Windows ARM64"

    $vcvarsArm64 = $null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -property installationPath 2>$null
        if ($installPath) {
            $vcvarsArm64 = Join-Path $installPath "VC\Auxiliary\Build\vcvarsarm64.bat"
        }
    }

    if (-not $vcvarsArm64 -or -not (Test-Path $vcvarsArm64)) {
        Write-Warning "ARM64 build tools not found. Skipping Windows ARM64 build."
        Write-Warning "Install with: vs_installer modify --add Microsoft.VisualStudio.Component.VC.ARM64"
    } else {
        # Build ARM64 using vcvarsarm64
        cmd /c "`"$vcvarsArm64`" && `"$ProjectRoot\build.bat`" arm64"
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Windows ARM64 build failed"
            exit 1
        }

        $zipName = "NCD-$Version-windows-arm64.zip"
        $zipPath = Join-Path $DistDir $zipName
        $tempDir = Join-Path $env:TEMP "ncd_pkg_win_arm64_$([Guid]::NewGuid().ToString('N').Substring(0,8))"
        Ensure-Directory $tempDir

        Copy-Item (Join-Path $ProjectRoot "NewChangeDirectory_arm64.exe") $tempDir
        Copy-Item (Join-Path $ProjectRoot "NCDService_arm64.exe") $tempDir
        Copy-Item (Join-Path $ProjectRoot "ncd.bat") $tempDir
        Copy-Item (Join-Path $ProjectRoot "ncd_service.bat") $tempDir
        Copy-Item (Join-Path $PackagingDir "windows\install.bat") $tempDir
        Copy-Item (Join-Path $ProjectRoot "README.md") $tempDir

        if (Test-Path (Join-Path $ProjectRoot "completions")) {
            Copy-Item -Recurse (Join-Path $ProjectRoot "completions") $tempDir
        }

        Compress-Archive -Path "$tempDir\*" -DestinationPath $zipPath -Force
        Remove-Item -Recurse -Force $tempDir

        Write-Host "Created: $zipPath" -ForegroundColor Green
    }
}

# ========================================================================
# Phase 3: Build Linux x64
# ========================================================================

if (-not $SkipLinuxBuild) {
    Write-Header "Building Linux x64"

    $wslCheck = wsl bash -c "which gcc" 2>$null
    if (-not $wslCheck) {
        Write-Warning "WSL gcc not found. Skipping Linux x64 build."
    } else {
        wsl bash -c "cd '$($ProjectRoot -replace '\\','/')' && ./build.sh x64"
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Linux x64 build failed"
            exit 1
        }

        $tarName = "NCD-$Version-linux-x64.tar.gz"
        $tarPath = Join-Path $DistDir $tarName
        $tempDir = Join-Path $env:TEMP "ncd_pkg_linux_x64_$([Guid]::NewGuid().ToString('N').Substring(0,8))"
        Ensure-Directory $tempDir

        Copy-Item (Join-Path $ProjectRoot "NewChangeDirectory") $tempDir
        Copy-Item (Join-Path $ProjectRoot "NCDService") $tempDir
        Copy-Item (Join-Path $ProjectRoot "ncd") $tempDir
        Copy-Item (Join-Path $ProjectRoot "ncd_service") $tempDir
        Copy-Item (Join-Path $PackagingDir "linux\install.sh") $tempDir
        Copy-Item (Join-Path $ProjectRoot "README.md") $tempDir

        if (Test-Path (Join-Path $ProjectRoot "completions")) {
            Copy-Item -Recurse (Join-Path $ProjectRoot "completions") $tempDir
        }

        # Create tar.gz via WSL
        $wslTemp = wsl wslpath -u "$tempDir"
        wsl bash -c "cd '$wslTemp' && tar -czf '$($tarPath -replace '\\','/')' ."

        Remove-Item -Recurse -Force $tempDir
        Write-Host "Created: $tarPath" -ForegroundColor Green
    }
}

# ========================================================================
# Phase 4: Build Linux ARM64
# ========================================================================

if (-not $SkipLinuxBuild -and -not $SkipLinuxArm64) {
    Write-Header "Building Linux ARM64"

    $crossCheck = wsl bash -c "which aarch64-linux-gnu-gcc" 2>$null
    if (-not $crossCheck) {
        Write-Warning "aarch64-linux-gnu-gcc not found in WSL. Skipping Linux ARM64 build."
        Write-Warning "Install with: wsl sudo apt-get install gcc-aarch64-linux-gnu"
    } else {
        wsl bash -c "cd '$($ProjectRoot -replace '\\','/')' && CC_ARM64=aarch64-linux-gnu-gcc ./build.sh arm64"
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Linux ARM64 build failed"
            exit 1
        }

        $tarName = "NCD-$Version-linux-arm64.tar.gz"
        $tarPath = Join-Path $DistDir $tarName
        $tempDir = Join-Path $env:TEMP "ncd_pkg_linux_arm64_$([Guid]::NewGuid().ToString('N').Substring(0,8))"
        Ensure-Directory $tempDir

        Copy-Item (Join-Path $ProjectRoot "NewChangeDirectory_arm64") $tempDir
        Copy-Item (Join-Path $ProjectRoot "NCDService_arm64") $tempDir
        Copy-Item (Join-Path $ProjectRoot "ncd") $tempDir
        Copy-Item (Join-Path $ProjectRoot "ncd_service") $tempDir
        Copy-Item (Join-Path $PackagingDir "linux\install.sh") $tempDir
        Copy-Item (Join-Path $ProjectRoot "README.md") $tempDir

        if (Test-Path (Join-Path $ProjectRoot "completions")) {
            Copy-Item -Recurse (Join-Path $ProjectRoot "completions") $tempDir
        }

        $wslTemp = wsl wslpath -u "$tempDir"
        wsl bash -c "cd '$wslTemp' && tar -czf '$($tarPath -replace '\\','/')' ."

        Remove-Item -Recurse -Force $tempDir
        Write-Host "Created: $tarPath" -ForegroundColor Green
    }
}

# ========================================================================
# Phase 5: Build Linux RISC-V
# ========================================================================

if (-not $SkipLinuxBuild -and -not $SkipLinuxRiscv) {
    Write-Header "Building Linux RISC-V"

    $crossCheck = wsl bash -c "which riscv64-linux-gnu-gcc" 2>$null
    if (-not $crossCheck) {
        Write-Warning "riscv64-linux-gnu-gcc not found in WSL. Skipping Linux RISC-V build."
        Write-Warning "Install with: wsl sudo apt-get install gcc-riscv64-linux-gnu"
    } else {
        wsl bash -c "cd '$($ProjectRoot -replace '\\','/')' && CC_RISCV=riscv64-linux-gnu-gcc ./build.sh riscv64"
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Linux RISC-V build failed"
            exit 1
        }

        $tarName = "NCD-$Version-linux-riscv64.tar.gz"
        $tarPath = Join-Path $DistDir $tarName
        $tempDir = Join-Path $env:TEMP "ncd_pkg_linux_riscv64_$([Guid]::NewGuid().ToString('N').Substring(0,8))"
        Ensure-Directory $tempDir

        Copy-Item (Join-Path $ProjectRoot "NewChangeDirectory_riscv64") $tempDir
        Copy-Item (Join-Path $ProjectRoot "NCDService_riscv64") $tempDir
        Copy-Item (Join-Path $ProjectRoot "ncd") $tempDir
        Copy-Item (Join-Path $ProjectRoot "ncd_service") $tempDir
        Copy-Item (Join-Path $PackagingDir "linux\install.sh") $tempDir
        Copy-Item (Join-Path $ProjectRoot "README.md") $tempDir

        if (Test-Path (Join-Path $ProjectRoot "completions")) {
            Copy-Item -Recurse (Join-Path $ProjectRoot "completions") $tempDir
        }

        $wslTemp = wsl wslpath -u "$tempDir"
        wsl bash -c "cd '$wslTemp' && tar -czf '$($tarPath -replace '\\','/')' ."

        Remove-Item -Recurse -Force $tempDir
        Write-Host "Created: $tarPath" -ForegroundColor Green
    }
}

# ========================================================================
# Summary
# ========================================================================

Write-Header "Packaging Complete"

$packages = Get-ChildItem $DistDir | Select-Object Name, @{N="Size";E={"{0:N1} MB" -f ($_.Length/1MB)}}
if ($packages) {
    $packages | Format-Table -AutoSize
} else {
    Write-Warning "No packages were created."
}

Write-Host "Packages are in: $DistDir" -ForegroundColor Green
