#Requires -Version 5.1
<#
.SYNOPSIS
    Comprehensive environment check and repair tool for NCD tests.

.DESCRIPTION
    Diagnoses and fixes environment variable issues after interrupted tests.
    More comprehensive than the batch file version.

.PARAMETER Repair
    Attempt to repair any issues found.

.PARAMETER Verbose
    Show detailed information.

.EXAMPLE
    .\Check-Environment.ps1
    Shows current environment state

.EXAMPLE
    .\Check-Environment.ps1 -Repair
    Fixes any issues found

.EXAMPLE
    .\Check-Environment.ps1 -Verbose
    Shows detailed diagnostics
#>
[CmdletBinding()]
param(
    [switch]$Repair,
    [switch]$Verbose
)

if ($Verbose) {
    $VerbosePreference = 'Continue'
}

# ============================================================================
# Configuration
# ============================================================================

$colors = @{
    Success = 'Green'
    Error = 'Red'
    Warning = 'Yellow'
    Info = 'Cyan'
    Normal = 'White'
}

# ============================================================================
# Helper Functions
# ============================================================================

function Write-Section {
    param([string]$Title)
    Write-Host "`n========================================" -ForegroundColor $colors.Info
    Write-Host $Title -ForegroundColor $colors.Info
    Write-Host "========================================" -ForegroundColor $colors.Info
}

function Write-Finding {
    param(
        [string]$Message,
        [string]$Status = 'Info',
        [string]$Details = ''
    )
    
    $color = $colors[$Status]
    $prefix = switch ($Status) {
        'Success' { '[OK]   ' }
        'Error'   { '[FAIL] ' }
        'Warning' { '[WARN] ' }
        default   { '[INFO] ' }
    }
    
    Write-Host "$prefix$Message" -ForegroundColor $color
    if ($Details) {
        Write-Host "       $Details" -ForegroundColor Gray
    }
}

function Get-ExpectedLocalAppData {
    return Join-Path $env:USERPROFILE "AppData\Local"
}

function Test-IsTestTempPath {
    param([string]$Path)
    return $Path -match 'temp[/\\]ncd' -or 
           $Path -match 'ncd_test_' -or
           $Path -match 'ncd_.*_test_'
}

# ============================================================================
# Check Functions
# ============================================================================

function Test-Environment {
    $results = @{
        IsClean = $true
        Issues = @()
        Details = @{}
    }
    
    Write-Section "Environment Diagnostics"
    
    # Check 1: LOCALAPPDATA
    Write-Host "`n[1] Checking LOCALAPPDATA..." -ForegroundColor $colors.Info
    $localAppData = $env:LOCALAPPDATA
    $expected = Get-ExpectedLocalAppData
    $results.Details['LOCALAPPDATA'] = $localAppData
    $results.Details['Expected_LOCALAPPDATA'] = $expected
    
    if (Test-IsTestTempPath -Path $localAppData) {
        Write-Finding "LOCALAPPDATA points to test temp directory!" 'Error'
        Write-Host "       Current:  $localAppData" -ForegroundColor Yellow
        Write-Host "       Expected: $expected" -ForegroundColor Yellow
        $results.IsClean = $false
        $results.Issues += "LOCALAPPDATA_CORRUPTED"
    }
    elseif ($localAppData -ne $expected) {
        Write-Finding "LOCALAPPDATA is non-standard" 'Warning'
        Write-Host "       Current:  $localAppData" -ForegroundColor Yellow
        Write-Host "       Expected: $expected" -ForegroundColor Yellow
    }
    else {
        Write-Finding "LOCALAPPDATA is correct" 'Success'
    }
    
    # Check 2: NCD_TEST_MODE
    Write-Host "`n[2] Checking NCD_TEST_MODE..." -ForegroundColor $colors.Info
    $results.Details['NCD_TEST_MODE'] = $env:NCD_TEST_MODE
    
    if ($env:NCD_TEST_MODE -eq '1') {
        Write-Finding "NCD_TEST_MODE is set to 1" 'Warning'
        Write-Host "       This disables background rescans (usually harmless)" -ForegroundColor Gray
    }
    elseif ($env:NCD_TEST_MODE) {
        Write-Finding "NCD_TEST_MODE has unexpected value: $($env:NCD_TEST_MODE)" 'Warning'
    }
    else {
        Write-Finding "NCD_TEST_MODE is not set (normal)" 'Success'
    }
    
    # Check 3: NCD Database
    Write-Host "`n[3] Checking NCD Database..." -ForegroundColor $colors.Info
    $ncdPath = Join-Path $env:LOCALAPPDATA "NCD"
    $dbFiles = Get-ChildItem -Path $ncdPath -Filter "ncd_*.database" -ErrorAction SilentlyContinue
    
    if ($dbFiles) {
        Write-Finding "Found NCD database files" 'Success'
        Write-Host "       Location: $ncdPath" -ForegroundColor Gray
        foreach ($file in $dbFiles | Select-Object -First 3) {
            $size = if ($file.Length -gt 1MB) { 
                "{0:N2} MB" -f ($file.Length / 1MB)
            } else { 
                "{0:N0} KB" -f ($file.Length / 1KB)
            }
            Write-Host "       - $($file.Name) ($size)" -ForegroundColor Gray
        }
        if ($dbFiles.Count -gt 3) {
            Write-Host "       ... and $($dbFiles.Count - 3) more" -ForegroundColor Gray
        }
        $results.Details['DatabasePath'] = $ncdPath
        $results.Details['DatabaseCount'] = $dbFiles.Count
    }
    else {
        Write-Finding "No NCD database found" 'Warning'
        Write-Host "       Expected at: $ncdPath" -ForegroundColor Gray
        Write-Host "       Run 'ncd -r' to create database" -ForegroundColor Gray
        $results.Issues += "NO_DATABASE"
    }
    
    # Check 4: Orphaned Test Directories
    Write-Host "`n[4] Checking for orphaned test directories..." -ForegroundColor $colors.Info
    $patterns = @('ncd_test_*', 'ncd_*_test_*', 'ncd_unit_test_*', 'ncd_master_test_*')
    $orphaned = @()
    
    foreach ($pattern in $patterns) {
        $found = Get-ChildItem -Path $env:TEMP -Directory -Filter $pattern -ErrorAction SilentlyContinue
        $orphaned += $found
    }
    
    if ($orphaned.Count -gt 0) {
        Write-Finding "Found $($orphaned.Count) orphaned test directories" 'Warning'
        foreach ($dir in $orphaned | Select-Object -First 5) {
            $age = ((Get-Date) - $dir.CreationTime).TotalHours
            $ageStr = if ($age -lt 1) { "{0:N0} minutes" -f ($age * 60) } else { "{0:N1} hours" -f $age }
            Write-Host "       - $($dir.Name) (created $ageStr ago)" -ForegroundColor Gray
        }
        if ($orphaned.Count -gt 5) {
            Write-Host "       ... and $($orphaned.Count - 5) more" -ForegroundColor Gray
        }
        $results.IsClean = $false
        $results.Issues += "ORPHANED_DIRS"
        $results.Details['OrphanedDirs'] = $orphaned
    }
    else {
        Write-Finding "No orphaned test directories" 'Success'
    }
    
    # Check 5: Running NCD Processes
    Write-Host "`n[5] Checking for running NCD processes..." -ForegroundColor $colors.Info
    $ncdProcesses = Get-Process -Name "NCDService", "NewChangeDirectory" -ErrorAction SilentlyContinue
    
    if ($ncdProcesses) {
        Write-Finding "Found running NCD processes" 'Warning'
        foreach ($proc in $ncdProcesses) {
            $runtime = (Get-Date) - $proc.StartTime
            Write-Host "       - $($proc.Name) (PID: $($proc.Id), running for {0:N1} hours)" -f $runtime.TotalHours -ForegroundColor Gray
        }
        $results.Issues += "RUNNING_PROCESSES"
    }
    else {
        Write-Finding "No NCD processes running" 'Success'
    }
    
    # Check 6: TEMP Environment
    Write-Host "`n[6] Checking TEMP environment..." -ForegroundColor $colors.Info
    $results.Details['TEMP'] = $env:TEMP
    $results.Details['TMP'] = $env:TMP
    
    if (Test-IsTestTempPath -Path $env:TEMP) {
        Write-Finding "TEMP points to test temp directory!" 'Error'
        $results.IsClean = $false
        $results.Issues += "TEMP_CORRUPTED"
    }
    else {
        Write-Finding "TEMP is correct" 'Success'
    }
    
    return $results
}

function Repair-Environment {
    param([hashtable]$CheckResults)
    
    Write-Section "Environment Repair"
    
    $fixed = 0
    $failed = 0
    
    # Fix 1: LOCALAPPDATA
    if ($CheckResults.Issues -contains 'LOCALAPPDATA_CORRUPTED') {
        Write-Host "`nRepairing LOCALAPPDATA..." -ForegroundColor $colors.Info
        $correct = Get-ExpectedLocalAppData
        Write-Host "  Setting LOCALAPPDATA to: $correct" -ForegroundColor Gray
        [Environment]::SetEnvironmentVariable('LOCALAPPDATA', $correct, 'Process')
        $fixed++
    }
    
    # Fix 2: NCD_TEST_MODE
    if ($env:NCD_TEST_MODE) {
        Write-Host "`nClearing NCD_TEST_MODE..." -ForegroundColor $colors.Info
        Write-Host "  Previous value: $env:NCD_TEST_MODE" -ForegroundColor Gray
        Remove-Item Env:\NCD_TEST_MODE -ErrorAction SilentlyContinue
        $fixed++
    }
    
    # Fix 3: Orphaned directories
    if ($CheckResults.Issues -contains 'ORPHANED_DIRS') {
        Write-Host "`nRemoving orphaned test directories..." -ForegroundColor $colors.Info
        foreach ($dir in $CheckResults.Details['OrphanedDirs']) {
            try {
                Remove-Item -Path $dir.FullName -Recurse -Force -ErrorAction Stop
                Write-Host "  Removed: $($dir.Name)" -ForegroundColor Gray
                $fixed++
            }
            catch {
                Write-Warning "Could not remove $($dir.Name): $_"
                $failed++
            }
        }
    }
    
    # Fix 4: Running processes
    if ($CheckResults.Issues -contains 'RUNNING_PROCESSES') {
        Write-Host "`nStopping NCD processes..." -ForegroundColor $colors.Info
        Get-Process -Name "NCDService" -ErrorAction SilentlyContinue | 
            Stop-Process -Force -ErrorAction SilentlyContinue
        & wsl pkill -9 -f "ncd_service" 2>&1 | Out-Null
        Write-Host "  Processes stopped" -ForegroundColor Gray
        $fixed++
    }
    
    # Summary
    Write-Host "`n========================================" -ForegroundColor $colors.Info
    Write-Host "Repair Summary" -ForegroundColor $colors.Info
    Write-Host "========================================" -ForegroundColor $colors.Info
    Write-Host "Fixed:  $fixed" -ForegroundColor $(if ($fixed -gt 0) { 'Green' } else { 'White' })
    Write-Host "Failed: $failed" -ForegroundColor $(if ($failed -gt 0) { 'Red' } else { 'White' })
    
    if ($failed -eq 0) {
        Write-Host "`nEnvironment repaired successfully!" -ForegroundColor $colors.Success
    }
    else {
        Write-Host "`nSome repairs failed. You may need to:" -ForegroundColor $colors.Warning
        Write-Host "  1. Close and reopen your command prompt" -ForegroundColor Gray
        Write-Host "  2. Run as Administrator" -ForegroundColor Gray
    }
}

# ============================================================================
# Main
# ============================================================================

Write-Section "NCD Environment Check"

# Run checks
$results = Test-Environment

# Show summary
Write-Section "Summary"

if ($results.IsClean -and $results.Issues.Count -eq 0) {
    Write-Host "`n✓ Environment is CLEAN and ready for NCD usage." -ForegroundColor $colors.Success
}
elseif ($results.IsClean) {
    Write-Host "`n⚠ Environment has minor issues (warnings only)." -ForegroundColor $colors.Warning
}
else {
    Write-Host "`n✗ Environment has ISSUES that need attention." -ForegroundColor $colors.Error
    Write-Host "`nIssues found:" -ForegroundColor $colors.Error
    foreach ($issue in $results.Issues) {
        Write-Host "  - $issue" -ForegroundColor Yellow
    }
}

# Repair if requested
if ($Repair) {
    if ($results.IsClean -and $results.Issues.Count -eq 0) {
        Write-Host "`nNo repairs needed." -ForegroundColor $colors.Success
    }
    else {
        Repair-Environment -CheckResults $results
        
        # Verify
        Write-Host "`nVerifying repairs..." -ForegroundColor $colors.Info
        $newResults = Test-Environment
        
        if ($newResults.IsClean) {
            Write-Host "`n✓ Environment is now clean!" -ForegroundColor $colors.Success
        }
        else {
            Write-Host "`n⚠ Some issues could not be repaired automatically." -ForegroundColor $colors.Warning
        }
    }
}
else {
    if (-not $results.IsClean -or $results.Issues.Count -gt 0) {
        Write-Host "`nTo repair these issues, run:" -ForegroundColor $colors.Info
        Write-Host "  .\Check-Environment.ps1 -Repair" -ForegroundColor White
    }
}

# Final recommendations
Write-Host "`nRecommendations:" -ForegroundColor $colors.Info
if ($results.Details['DatabaseCount'] -eq 0) {
    Write-Host "  - Run 'ncd -r' to create your database" -ForegroundColor Gray
}
Write-Host "  - Use '.\Run-Tests-Safe.bat' to run tests safely" -ForegroundColor Gray
Write-Host "  - Or use '.\Run-NcdTests.ps1' for PowerShell-native testing" -ForegroundColor Gray

exit $(if ($results.IsClean) { 0 } else { 1 })
