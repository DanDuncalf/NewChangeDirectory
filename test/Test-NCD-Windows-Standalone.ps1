# Test-NCD-Windows-Standalone.ps1 -- NCD client tests on Windows WITHOUT service
# 
# PURPOSE:
#   Tests NCD client in standalone/disk-based state access mode.
#   Verifies that NCD works correctly without the resident service running.
#
# REQUIREMENTS:
#   - NewChangeDirectory.exe built in project root
#   - Run from project root: .\test\Test-NCD-Windows-Standalone.ps1

param(
    [switch]$VerboseOutput
)

$ErrorActionPreference = "Stop"
$script:TestsRun = 0
$script:TestsPassed = 0
$script:TestsFailed = 0

function Write-TestLog {
    param([string]$Message, [string]$Level = "INFO")
    $prefix = switch ($Level) {
        "PASS" { "[PASS]" }
        "FAIL" { "[FAIL]" }
        "SKIP" { "[SKIP]" }
        default { "       " }
    }
    Write-Host "$prefix $Message"
}

function Invoke-NcdCommand {
    param(
        [string[]]$Arguments,
        [int]$TimeoutSeconds = 30,
        [string]$WorkingDirectory = $null
    )
    
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $script:NcdExe
    $psi.Arguments = $Arguments -join " "
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.EnvironmentVariables["NCD_TEST_MODE"] = "1"
    $psi.EnvironmentVariables["LOCALAPPDATA"] = "$script:TestData"
    if ($WorkingDirectory) {
        $psi.WorkingDirectory = $WorkingDirectory
    } elseif ($script:DefaultWorkingDirectory) {
        $psi.WorkingDirectory = $script:DefaultWorkingDirectory
    }
    
    $proc = [System.Diagnostics.Process]::Start($psi)
    $completed = $proc.WaitForExit($TimeoutSeconds * 1000)
    
    if (-not $completed) {
        $proc.Kill()
        $proc.WaitForExit(1000)
        return @{ ExitCode = 124; Output = ""; Error = "Timeout after ${TimeoutSeconds}s" }
    }
    
    $output = $proc.StandardOutput.ReadToEnd()
    $error = $proc.StandardError.ReadToEnd()
    
    return @{ ExitCode = $proc.ExitCode; Output = $output; Error = $error }
}

function Test-Condition {
    param(
        [string]$TestName,
        [scriptblock]$Condition,
        [string]$FailureReason = ""
    )
    
    $script:TestsRun++
    try {
        $result = & $Condition
        if ($result) {
            $script:TestsPassed++
            Write-TestLog $TestName "PASS"
        } else {
            $script:TestsFailed++
            Write-TestLog "$TestName - $FailureReason" "FAIL"
        }
    } catch {
        $script:TestsFailed++
        Write-TestLog "$TestName - Error: $_" "FAIL"
    }
}

# ============================================================================
# Main Test Script
# ============================================================================

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$script:NcdExe = Join-Path $ProjectRoot "NewChangeDirectory.exe"
$ServiceExe = Join-Path $ProjectRoot "NCDService.exe"

Write-Host "========================================"
Write-Host "NCD Client Tests - Windows (Standalone)"
Write-Host "========================================"
Write-Host ""

# Verify executable exists
if (-not (Test-Path $script:NcdExe)) {
    Write-Error "NCD executable not found: $script:NcdExe"
    exit 1
}

# Setup test environment
$script:TestData = Join-Path $env:TEMP ("ncd_ncd_test_" + (Get-Random))
$TestRoot = Join-Path $env:TEMP ("ncd_test_tree_" + (Get-Random))
$script:DefaultWorkingDirectory = $TestRoot

Write-Host "NCD: $script:NcdExe"
Write-Host "Test Data: $script:TestData"
Write-Host "Test Root: $TestRoot"
Write-Host ""

# Cleanup function
function Cleanup-TestEnvironment {
    Write-Host "Cleaning up test environment..."
    
    # Stop service if running
    if (Test-Path $ServiceExe) {
        & $ServiceExe stop 2>$null | Out-Null
        Start-Sleep -Seconds 1
        Get-Process NCDService -ErrorAction SilentlyContinue | Stop-Process -Force 2>$null
    }
    
    # Remove test directories
    if (Test-Path $script:TestData) {
        Remove-Item -Recurse -Force $script:TestData -ErrorAction SilentlyContinue
        Write-Host "  Removed test data directory"
    }
    if (Test-Path $TestRoot) {
        Remove-Item -Recurse -Force $TestRoot -ErrorAction SilentlyContinue
        Write-Host "  Removed test tree directory"
    }
    
    Write-Host "  Cleanup complete."
}

# Ensure cleanup on exit
try {
    # Create test directories
    New-Item -ItemType Directory -Path "$script:TestData\NCD" -Force | Out-Null
    New-Item -ItemType Directory -Path $TestRoot -Force | Out-Null
    
    # Create minimal metadata file
    [IO.File]::WriteAllBytes("$script:TestData\NCD\ncd.metadata", 
        [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))
    
    # Ensure service is NOT running
    Write-Host "Ensuring service is NOT running..."
    if (Test-Path $ServiceExe) {
        & $ServiceExe stop 2>$null | Out-Null
        Start-Sleep -Seconds 1
        Get-Process NCDService -ErrorAction SilentlyContinue | Stop-Process -Force 2>$null
    }
    Write-Host "  [INFO] Service cleanup complete."
    
    # Verify standalone mode
    Write-Host "Verifying standalone mode..."
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/agent", "check", "--service-status")
    $svcStatus = $result.Output.Trim()
    Write-Host "  Service status: $svcStatus"
    Write-Host ""
    
    # Create test directory tree
    Write-Host "Setting up test environment..."
    New-Item -ItemType Directory -Path "$TestRoot\Projects\alpha\src" -Force | Out-Null
    New-Item -ItemType Directory -Path "$TestRoot\Projects\beta\src" -Force | Out-Null
    New-Item -ItemType Directory -Path "$TestRoot\Users\scott\Downloads" -Force | Out-Null
    New-Item -ItemType Directory -Path "$TestRoot\Users\scott\Documents" -Force | Out-Null
    New-Item -ItemType Directory -Path "$TestRoot\Data\Files" -Force | Out-Null
    Write-Host "  Created test directory tree"
    Write-Host ""
    
    # Initial scan from the isolated test tree
    Write-Host "Performing initial scan of test tree..."
    $scanResult = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/r.") -TimeoutSeconds 30 -WorkingDirectory $TestRoot
    Write-Host "  Scan complete."
    Write-Host ""
    
    # ============================================================================
    # Main Test Suite
    # ============================================================================
    
    Write-Host "--- Test Suite: Standalone Mode Operations ---"
    Write-Host ""
    
    # Test 1: Help output
    Write-Host "[TEST 1] Help with /h"
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/h")
    Test-Condition "Help with /h" { $result.ExitCode -eq 0 } "exit code $($result.ExitCode)"
    
    # Test 2: Version information
    Write-Host "[TEST 2] Version with /v"
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/v")
    Test-Condition "Version with /v" { $result.ExitCode -eq 0 } "exit code $($result.ExitCode)"
    
    # Test 3: Service status shows NOT_RUNNING
    Write-Host "[TEST 3] Service status shows NOT_RUNNING"
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/agent", "check", "--service-status")
    Test-Condition "Service status shows NOT_RUNNING" { 
        $result.Output -match "NOT_RUNNING" 
    } "output: $($result.Output)"
    
    # Test 4: Basic search finds directory
    Write-Host "[TEST 4] Basic search finds directory"
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/agent", "query", "Downloads", "--limit", "1") -WorkingDirectory $TestRoot
    Write-Host "  Query output: [$($result.Output)]"
    Write-Host "  Query exit code: $($result.ExitCode)"
    Test-Condition "Basic search finds directory" { 
        $result.Output -match "Downloads" 
    } "output: $($result.Output)"
    
    # Test 5: Multi-component search
    Write-Host "[TEST 5] Multi-component search"
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/agent", "query", "scott\Downloads", "--limit", "1") -WorkingDirectory $TestRoot
    Test-Condition "Multi-component search" { 
        $result.Output -match "Downloads" 
    } "output: $($result.Output)"
    
    # Test 6: Agent query command
    Write-Host "[TEST 6] Agent query command"
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/agent", "query", "Downloads") -WorkingDirectory $TestRoot
    Test-Condition "Agent query command" { 
        $result.Output -match "Downloads" 
    } "output: $($result.Output)"
    
    # Test 7: Agent check database age
    Write-Host "[TEST 7] Agent check database age"
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/agent", "check", "--db-age") -WorkingDirectory $TestRoot
    Test-Condition "Agent check database age" { $result.ExitCode -eq 0 } "exit code $($result.ExitCode)"
    
    # Test 8: Agent check stats
    Write-Host "[TEST 8] Agent check stats"
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/agent", "check", "--stats") -WorkingDirectory $TestRoot
    Test-Condition "Agent check stats" { $result.ExitCode -eq 0 } "exit code $($result.ExitCode)"
    
    # Test 9: Rescan creates database
    Write-Host "[TEST 9] Rescan creates database"
    Remove-Item "$script:TestData\NCD\ncd_*.database" -ErrorAction SilentlyContinue
    $scanResult = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/r.") -TimeoutSeconds 30 -WorkingDirectory $TestRoot
    $dbFound = Test-Path "$script:TestData\NCD\ncd_*.database"
    Test-Condition "Rescan creates database" { $dbFound } "no database file found"
    
    # Test 10: Search after rescan
    Write-Host "[TEST 10] Search after rescan"
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/agent", "query", "Projects", "--limit", "1") -WorkingDirectory $TestRoot
    Test-Condition "Search after rescan" { 
        $result.Output -match "Projects" 
    } "output: $($result.Output)"
    
    # Test 11: No match returns error
    Write-Host "[TEST 11] No match returns error"
    $result = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "NONEXISTENT_DIR_12345") -TimeoutSeconds 10
    Test-Condition "No match returns error" { $result.ExitCode -ne 0 } "exit code $($result.ExitCode) (expected non-zero)"
    
    # Test 12: Database override /d
    Write-Host "[TEST 12] Database override /d"
    $customDb = Join-Path $env:TEMP ("ncd_test_" + (Get-Random) + ".db")
    Push-Location $TestRoot
    $scanResult = Invoke-NcdCommand -Arguments @("-conf", "$script:TestData\NCD\ncd.metadata", "/r.", "/d", $customDb) -TimeoutSeconds 30
    Pop-Location
    $dbCreated = Test-Path $customDb
    Test-Condition "Database override /d" { $dbCreated } "custom database not created"
    if (Test-Path $customDb) { Remove-Item $customDb -ErrorAction SilentlyContinue }
    
} finally {
    Cleanup-TestEnvironment
}

# Summary
Write-Host ""
Write-Host "========================================"
Write-Host "Test Summary"
Write-Host "========================================"
Write-Host "  Total:   $script:TestsRun"
Write-Host "  Passed:  $script:TestsPassed"
Write-Host "  Failed:  $script:TestsFailed"
Write-Host ""

if ($script:TestsFailed -gt 0) {
    Write-Host "RESULT: FAILED"
    exit 1
} else {
    Write-Host "RESULT: PASSED"
    exit 0
}
