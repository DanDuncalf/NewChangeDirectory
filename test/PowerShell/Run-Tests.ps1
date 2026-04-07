# Run-Tests.ps1 - PowerShell test runner for NCD
# This script runs unit tests on Windows

$ErrorActionPreference = "Stop"

# Disable NCD background rescans to prevent scanning user drives during tests
$env:NCD_TEST_MODE = "1"

$TestDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $TestDir

function Write-Header($text) {
    Write-Host "`n=== $text ===" -ForegroundColor Cyan
}

function Write-Pass($text) {
    Write-Host "  PASS: $text" -ForegroundColor Green
}

function Write-Fail($text) {
    Write-Host "  FAIL: $text" -ForegroundColor Red
}

# Check for Visual Studio
Write-Header "Environment Check"
$cl = Get-Command cl -ErrorAction SilentlyContinue
if ($cl) {
    Write-Pass "MSVC (cl.exe) found in PATH"
} else {
    Write-Fail "MSVC (cl.exe) not found. Run from VS Developer Command Prompt."
    exit 1
}

# Build test executables
Write-Header "Building Tests"

$TestSources = @(
    @{ Name = "test_database"; Sources = @("test_database.c", "test_framework.c", "..\src\database.c") },
    @{ Name = "test_matcher"; Sources = @("test_matcher.c", "test_framework.c", "..\src\matcher.c", "..\src\database.c") },
    @{ Name = "test_bugs"; Sources = @("test_bugs.c", "test_framework.c", "..\src\matcher.c", "..\src\database.c") }
)

$BuiltTests = @()
foreach ($test in $TestSources) {
    $exeName = "$($test.Name).exe"
    $sourcePaths = $test.Sources | ForEach-Object { Join-Path $TestDir $_ }
    
    Write-Host "Building $exeName..."
    $cmd = "cl /nologo /W3 /O2 /Isrc /I. /D_CRT_SECURE_NO_WARNINGS /Fe:$exeName $($sourcePaths -join ' ')"
    
    try {
        Invoke-Expression $cmd 2>&1 | Out-Null
        if (Test-Path $exeName) {
            Write-Pass "Built $exeName"
            $BuiltTests += $exeName
        } else {
            Write-Fail "Failed to build $exeName"
        }
    } catch {
        Write-Fail "Build error for ${exeName}: $_"
    }
}

# Run tests
Write-Header "Running Tests"
$AllPassed = $true

foreach ($exe in $BuiltTests) {
    Write-Host "`nRunning $exe..."
    $exePath = Join-Path $TestDir $exe
    
    & $exePath
    $exitCode = $LASTEXITCODE
    
    if ($exitCode -eq 0) {
        Write-Pass "$exe completed"
    } else {
        Write-Fail "$exe failed with exit code $exitCode"
        $AllPassed = $false
    }
}

# Cleanup
Write-Header "Cleanup"
Get-ChildItem $TestDir -Filter "*.exe" | Remove-Item -Force
Get-ChildItem $TestDir -Filter "*.obj" | Remove-Item -Force
Write-Pass "Removed build artifacts"

# Summary
Write-Header "Summary"
if ($AllPassed) {
    Write-Host "All tests PASSED!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "Some tests FAILED!" -ForegroundColor Red
    exit 1
}
