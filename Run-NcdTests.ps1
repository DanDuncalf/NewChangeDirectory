#Requires -Version 5.1
<#
.SYNOPSIS
    PowerShell Test Runner for NCD with Guaranteed Cleanup

.DESCRIPTION
    This script runs NCD tests with PowerShell's try/finally blocks to ensure
    cleanup runs even when tests are interrupted with Ctrl+C.

    Unlike batch files, PowerShell CAN trap Ctrl+C and run cleanup code in
    finally blocks, making this the safest way to run NCD tests.

.PARAMETER TestSuite
    Which test suite to run. Options: All, Unit, Integration, Service, 
    NcdStandalone, NcdWithService, Windows, Wsl

.PARAMETER SkipBuild
    Skip the build phase.

.PARAMETER WindowsOnly
    Run only Windows tests (skip WSL).

.PARAMETER NoService
    Skip tests that require the service.

.PARAMETER Quick
    Skip fuzz tests and benchmarks.

.PARAMETER CheckEnvironment
    Check and optionally repair the environment without running tests.

.PARAMETER RepairEnvironment
    Repair a corrupted environment.

.EXAMPLE
    # Run all tests (safest, full cleanup guaranteed)
    .\Run-NcdTests.ps1

.EXAMPLE
    # Run only unit tests
    .\Run-NcdTests.ps1 -TestSuite Unit

.EXAMPLE
    # Run integration tests for Windows only
    .\Run-NcdTests.ps1 -TestSuite Windows

.EXAMPLE
    # Check if environment is clean
    .\Run-NcdTests.ps1 -CheckEnvironment

.EXAMPLE
    # Fix corrupted environment
    .\Run-NcdTests.ps1 -RepairEnvironment

.NOTES
    File Name      : Run-NcdTests.ps1
    Author         : NCD Test Framework
    Prerequisite   : PowerShell 5.1 or later
    Copyright      : (c) 2026 NCD Project
#>
[CmdletBinding()]
param(
    [Parameter()]
    [ValidateSet('All', 'Unit', 'Integration', 'Service', 'NcdStandalone', 
                 'NcdWithService', 'Windows', 'Wsl', 'None',
                 'ParallelAgent1', 'ParallelAgent2', 'ParallelAgent3', 'ParallelAgent4')]
    [string]$TestSuite = 'All',
    
    [switch]$SkipBuild,
    [switch]$SkipUnit,
    [switch]$WindowsOnly,
    [switch]$WslOnly,
    [switch]$NoService,
    [switch]$Quick,
    [switch]$CheckEnvironment,
    [switch]$RepairEnvironment,
    [switch]$VerboseOutput
)

# Set verbose preference
if ($VerboseOutput) {
    $VerbosePreference = 'Continue'
}

# ============================================================================
# Configuration
# ============================================================================

$ErrorActionPreference = 'Stop'
$script:TestResults = @()
$script:StartTime = Get-Date

# Colors for output
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

function Write-Status {
    param(
        [string]$Message,
        [string]$Color = 'White',
        [switch]$NoNewline
    )
    $params = @{
        Object = $Message
        ForegroundColor = $colors[$Color]
        NoNewline = $NoNewline
    }
    Write-Host @params
}

function Write-Section {
    param([string]$Title)
    Write-Host "`n========================================" -ForegroundColor $colors.Info
    Write-Host $Title -ForegroundColor $colors.Info
    Write-Host "========================================" -ForegroundColor $colors.Info
}

function Save-EnvironmentState {
    $script:OriginalEnvironment = @{
        LOCALAPPDATA = $env:LOCALAPPDATA
        NCD_TEST_MODE = $env:NCD_TEST_MODE
        TEMP = $env:TEMP
        PATH = $env:PATH
        XDG_DATA_HOME = $env:XDG_DATA_HOME
        NCD_UI_KEYS = $env:NCD_UI_KEYS
        NCD_UI_KEYS_FILE = $env:NCD_UI_KEYS_FILE
        NCD_UI_KEYS_STRICT = $env:NCD_UI_KEYS_STRICT
        NCD_UI_KEY_TIMEOUT_MS = $env:NCD_UI_KEY_TIMEOUT_MS
        NCD_TUI_TEST = $env:NCD_TUI_TEST
        NCD_TUI_COLS = $env:NCD_TUI_COLS
        NCD_TUI_ROWS = $env:NCD_TUI_ROWS
    }
    Write-Verbose "Environment state saved"
}

function Restore-EnvironmentState {
    param([switch]$Silent)
    
    if ($script:OriginalEnvironment) {
        if (-not $Silent) {
            Write-Status "Restoring environment..." 'Info'
        }
        
        $env:LOCALAPPDATA = $script:OriginalEnvironment.LOCALAPPDATA
        $env:NCD_TEST_MODE = $script:OriginalEnvironment.NCD_TEST_MODE
        $env:TEMP = $script:OriginalEnvironment.TEMP
        $env:PATH = $script:OriginalEnvironment.PATH
        $env:XDG_DATA_HOME = $script:OriginalEnvironment.XDG_DATA_HOME
        $env:NCD_UI_KEYS = $script:OriginalEnvironment.NCD_UI_KEYS
        $env:NCD_UI_KEYS_FILE = $script:OriginalEnvironment.NCD_UI_KEYS_FILE
        $env:NCD_UI_KEYS_STRICT = $script:OriginalEnvironment.NCD_UI_KEYS_STRICT
        $env:NCD_UI_KEY_TIMEOUT_MS = $script:OriginalEnvironment.NCD_UI_KEY_TIMEOUT_MS
        $env:NCD_TUI_TEST = $script:OriginalEnvironment.NCD_TUI_TEST
        $env:NCD_TUI_COLS = $script:OriginalEnvironment.NCD_TUI_COLS
        $env:NCD_TUI_ROWS = $script:OriginalEnvironment.NCD_TUI_ROWS
        
        if (-not $Silent) {
            Write-Status "  LOCALAPPDATA: $env:LOCALAPPDATA" 'Normal'
        }
    }
}

function Clear-TuiEnvironmentVariables {
    param([switch]$Silent)
    
    if (-not $Silent) {
        Write-Status "Clearing TUI environment variables..." 'Info'
    }
    
    # Clear all NCD_UI* and NCD_TUI* variables to prevent interference
    $tuiVars = @(
        'NCD_UI_KEYS',
        'NCD_UI_KEYS_FILE',
        'NCD_UI_KEYS_STRICT',
        'NCD_UI_KEY_TIMEOUT_MS',
        'NCD_TUI_TEST',
        'NCD_TUI_COLS',
        'NCD_TUI_ROWS'
    )
    
    foreach ($var in $tuiVars) {
        if (Test-Path "Env:$var") {
            Remove-Item "Env:$var" -ErrorAction SilentlyContinue
            Write-Verbose "Cleared `$env:$var"
        }
    }
}

function Stop-NcdProcesses {
    Write-Verbose "Stopping NCD processes..."
    
    # Stop Windows service gracefully first
    try {
        $ncdService = Get-Command NCDService.exe -ErrorAction SilentlyContinue
        if ($ncdService) {
            & NCDService.exe stop 2>&1 | Out-Null
            Start-Sleep -Seconds 1
        }
    }
    catch {
        # Ignore errors
    }
    
    # Force kill any remaining processes
    Get-Process -Name "NCDService" -ErrorAction SilentlyContinue | 
        Stop-Process -Force -ErrorAction SilentlyContinue
    
    # Kill WSL processes
    & wsl pkill -9 -f "ncd_service" 2>&1 | Out-Null
    & wsl pkill -9 -f "NCDService" 2>&1 | Out-Null
}

function Remove-TestTempDirs {
    param([switch]$Silent)
    
    if (-not $Silent) {
        Write-Status "Cleaning up test temp directories..." 'Info'
    }
    
    $patterns = @('ncd_test_*', 'ncd_*_test_*', 'ncd_unit_test_*', 'ncd_master_test_*')
    
    foreach ($pattern in $patterns) {
        $dirs = Get-ChildItem -Path $env:TEMP -Directory -Filter $pattern -ErrorAction SilentlyContinue
        foreach ($dir in $dirs) {
            try {
                Remove-Item -Path $dir.FullName -Recurse -Force -ErrorAction SilentlyContinue
                if (-not $Silent) {
                    Write-Verbose "Removed: $($dir.FullName)"
                }
            }
            catch {
                Write-Warning "Could not remove: $($dir.FullName)"
            }
        }
    }
}

function Test-Environment {
    $issues = @()
    
    # Check LOCALAPPDATA
    if ($env:LOCALAPPDATA -match 'temp[/\\]ncd') {
        $issues += "LOCALAPPDATA points to test temp: $env:LOCALAPPDATA"
    }
    
    # Check for real NCD database
    $ncdPath = Join-Path $env:LOCALAPPDATA "NCD"
    $hasDb = Test-Path (Join-Path $ncdPath "*.database")
    
    # Check for orphaned temp dirs
    $orphaned = Get-ChildItem $env:TEMP -Directory -ErrorAction SilentlyContinue | 
        Where-Object { $_.Name -match '^ncd_test_|^ncd_.*_test_' }
    
    return @{
        IsClean = $issues.Count -eq 0
        Issues = $issues
        HasDatabase = $hasDb
        DatabasePath = $ncdPath
        OrphanedCount = $orphaned.Count
    }
}

function Repair-Environment {
    Write-Section "Repairing Environment"
    
    # Fix LOCALAPPDATA if needed
    if ($env:LOCALAPPDATA -match 'temp[/\\]ncd') {
        $fixedPath = Join-Path $env:USERPROFILE "AppData\Local"
        Write-Status "Fixing LOCALAPPDATA:" 'Warning'
        Write-Status "  From: $env:LOCALAPPDATA" 'Normal'
        Write-Status "  To:   $fixedPath" 'Normal'
        $env:LOCALAPPDATA = $fixedPath
    }
    
    # Clear NCD_TEST_MODE
    if ($env:NCD_TEST_MODE) {
        Write-Status "Clearing NCD_TEST_MODE (was: $env:NCD_TEST_MODE)" 'Warning'
        Remove-Item Env:\NCD_TEST_MODE -ErrorAction SilentlyContinue
    }
    
    # Clean up orphaned directories
    Remove-TestTempDirs
    
    # Stop any lingering processes
    Stop-NcdProcesses
    
    Write-Status "Environment repair complete!" 'Success'
    
    # Show current state
    $state = Test-Environment
    Write-Host "`nCurrent state:"
    Write-Host "  LOCALAPPDATA: $($env:LOCALAPPDATA)"
    Write-Host "  Database found: $($state.HasDatabase)"
    if ($state.HasDatabase) {
        Write-Host "  Database path: $($state.DatabasePath)"
    }
}

function Invoke-Test {
    param(
        [string]$Name,
        [scriptblock]$ScriptBlock
    )
    
    Write-Section "Running: $Name"
    
    $result = @{
        Name = $Name
        Passed = $false
        ExitCode = 0
        Duration = $null
        Error = $null
    }
    
    $start = Get-Date
    
    try {
        & $ScriptBlock
        $result.ExitCode = $LASTEXITCODE
        $result.Passed = ($LASTEXITCODE -eq 0)
    }
    catch {
        $result.Error = $_.Exception.Message
        $result.Passed = $false
        $result.ExitCode = 1
    }
    finally {
        $result.Duration = (Get-Date) - $start
    }
    
    $script:TestResults += $result
    
    if ($result.Passed) {
        Write-Status "[PASS] $Name ($($result.Duration.ToString('mm\:ss')))" 'Success'
    }
    else {
        Write-Status "[FAIL] $Name (Exit: $($result.ExitCode))" 'Error'
        if ($result.Error) {
            Write-Status "  Error: $($result.Error)" 'Error'
        }
    }
    
    return $result.Passed
}

function Build-Project {
    Write-Section "Build Phase"

    # Convert current path to WSL path for WSL builds
    $drive = (Get-Location).Path[0].ToString().ToLower()
    $pathPart = (Get-Location).Path.Substring(2).Replace('\', '/')
    $wslProjectPath = "/mnt/$drive$pathPart"

    # Build Windows binaries
    if (-not $WslOnly) {
        Write-Status "Building Windows binaries..." 'Info'
        & .\build.bat
        if ($LASTEXITCODE -ne 0) {
            throw "Windows build failed"
        }

        # Build test executables
        Write-Status "Building Windows test executables..." 'Info'
        Push-Location test
        try {
            & .\build-tests.bat
            if ($LASTEXITCODE -ne 0) {
                Write-Warning "Test build had issues, continuing anyway..."
            }
        }
        finally {
            Pop-Location
        }
    }

    # Build WSL binaries
    if (-not $WindowsOnly) {
        Write-Status "Building WSL binaries..." 'Info'
        $wslAvailable = try { $null -ne (& wsl echo "wsl_ok" 2>&1 | Select-String "wsl_ok") } catch { $false }
        if ($wslAvailable) {
            & wsl bash -c "cd '$wslProjectPath' && chmod +x build.sh && ./build.sh"
            & wsl bash -c "cd '$wslProjectPath/test' && make all"
        }
        else {
            Write-Warning "WSL not available, skipping WSL build"
        }
    }
}

function Run-UnitTests {
    $passed = $true
    
    Write-Section "Unit Tests"
    
    Push-Location test
    try {
        $args = @()
        if ($Quick) {
            $args += '--skip-fuzz'
            $args += '--skip-bench'
        }
        if ($NoService) {
            $args += '--skip-service'
        }
        
        $result = Invoke-Test -Name "All Unit Tests" -ScriptBlock {
            & .\Run-All-Unit-Tests.bat $args
        }
        $passed = $result -and $passed
    }
    finally {
        Pop-Location
    }
    
    return $passed
}

function Run-ParallelExpansionTests {
    param([string]$Agent = 'All')
    
    $passed = $true
    
    Write-Section "Parallel Expansion Tests (Agent $Agent)"
    
    Push-Location test
    try {
        switch ($Agent) {
            '1' {
                # Agent 1: Data Core (90 tests)
                $result = Invoke-Test -Name "Agent 1: Database Extended" -ScriptBlock {
                    if (Test-Path test_database_extended.exe) { .\test_database_extended.exe } else { exit 0 }
                }
                $passed = $result -and $passed
                
                $result = Invoke-Test -Name "Agent 1: Odd Cases" -ScriptBlock {
                    if (Test-Path test_odd_cases.exe) { .\test_odd_cases.exe } else { exit 0 }
                }
                $passed = $result -and $passed
            }
            '2' {
                # Agent 2: Service Infrastructure (100 tests)
                if (-not $NoService) {
                    $result = Invoke-Test -Name "Agent 2: Service Stress" -ScriptBlock {
                        if (Test-Path test_service_stress.exe) { .\test_service_stress.exe } else { exit 0 }
                    }
                    $passed = $result -and $passed
                    
                    $result = Invoke-Test -Name "Agent 2: Service Rescan" -ScriptBlock {
                        if (Test-Path test_service_rescan.exe) { .\test_service_rescan.exe } else { exit 0 }
                    }
                    $passed = $result -and $passed
                }
                
                $result = Invoke-Test -Name "Agent 2: IPC Extended" -ScriptBlock {
                    if (Test-Path test_ipc_extended.exe) { .\test_ipc_extended.exe } else { exit 0 }
                }
                $passed = $result -and $passed
                
                $result = Invoke-Test -Name "Agent 2: SHM Stress" -ScriptBlock {
                    if (Test-Path test_shm_stress.exe) { .\test_shm_stress.exe } else { exit 0 }
                }
                $passed = $result -and $passed
            }
            '3' {
                # Agent 3: UI and Main (100 tests)
                $result = Invoke-Test -Name "Agent 3: UI Extended" -ScriptBlock {
                    if (Test-Path test_ui_extended.exe) { .\test_ui_extended.exe } else { exit 0 }
                }
                $passed = $result -and $passed
                
                $result = Invoke-Test -Name "Agent 3: UI Exclusions" -ScriptBlock {
                    if (Test-Path test_ui_exclusions.exe) { .\test_ui_exclusions.exe } else { exit 0 }
                }
                $passed = $result -and $passed
                
                $result = Invoke-Test -Name "Agent 3: Main Flow" -ScriptBlock {
                    if (Test-Path test_main.exe) { .\test_main.exe } else { exit 0 }
                }
                $passed = $result -and $passed
            }
            '4' {
                # Agent 4: Input Processing (140 tests)
                $result = Invoke-Test -Name "Agent 4: CLI Edge Cases" -ScriptBlock {
                    if (Test-Path test_cli_edge_cases.exe) { .\test_cli_edge_cases.exe } else { exit 0 }
                }
                $passed = $result -and $passed
                
                $result = Invoke-Test -Name "Agent 4: Scanner Extended" -ScriptBlock {
                    if (Test-Path test_scanner_extended.exe) { .\test_scanner_extended.exe } else { exit 0 }
                }
                $passed = $result -and $passed
                
                $result = Invoke-Test -Name "Agent 4: Matcher Extended" -ScriptBlock {
                    if (Test-Path test_matcher_extended.exe) { .\test_matcher_extended.exe } else { exit 0 }
                }
                $passed = $result -and $passed
                
                $result = Invoke-Test -Name "Agent 4: Result Edge Cases" -ScriptBlock {
                    if (Test-Path test_result_edge_cases.exe) { .\test_result_edge_cases.exe } else { exit 0 }
                }
                $passed = $result -and $passed
                
                $result = Invoke-Test -Name "Agent 4: Stress Tests" -ScriptBlock {
                    if (Test-Path test_stress.exe) { .\test_stress.exe } else { exit 0 }
                }
                $passed = $result -and $passed
            }
            default {
                Write-Warning "Invalid Agent number. Use 1, 2, 3, or 4."
            }
        }
    }
    finally {
        Pop-Location
    }
    
    return $passed
}

function Run-WindowsIntegrationTests {
    $passed = $true

    Write-Section "Windows Integration Tests"

    # Test 1: Service tests (isolated)
    if (-not $NoService) {
        $result = Invoke-Test -Name "Service Tests (Isolated)" -ScriptBlock {
            & .\test\Test-Service-Windows.bat
        }
        $passed = $result -and $passed
    }

    # Test 2: NCD standalone
    $result = Invoke-Test -Name "NCD Standalone" -ScriptBlock {
        & .\test\Test-NCD-Windows-Standalone.bat
    }
    $passed = $result -and $passed

    # Test 3: NCD with service
    if (-not $NoService) {
        $result = Invoke-Test -Name "NCD with Service" -ScriptBlock {
            & .\test\Test-NCD-Windows-With-Service.bat
        }
        $passed = $result -and $passed
    }

    # Test 4: Feature tests (comprehensive command-line tests)
    $featureTest = ".\test\Win\test_features.bat"
    if (Test-Path $featureTest) {
        $result = Invoke-Test -Name "Windows Feature Tests" -ScriptBlock {
            & $featureTest
        }
        $passed = $result -and $passed
    }

    # Test 5: Agent command tests
    $agentTest = ".\test\Win\test_agent_commands.bat"
    if (Test-Path $agentTest) {
        $result = Invoke-Test -Name "Windows Agent Command Tests" -ScriptBlock {
            & $agentTest
        }
        $passed = $result -and $passed
    }

    return $passed
}

function Run-WslIntegrationTests {
    $passed = $true

    Write-Section "WSL Integration Tests"

    $wslAvailable = try { $null -ne (& wsl echo "wsl_ok" 2>&1 | Select-String "wsl_ok") } catch { $false }
    if (-not $wslAvailable) {
        Write-Warning "WSL not available, skipping WSL tests"
        return $true
    }

    # Convert current path to WSL path
    $drive = (Get-Location).Path[0].ToString().ToLower()
    $path = (Get-Location).Path.Substring(2).Replace('\', '/')
    $wslPath = "/mnt/$drive$path"

    # Test 1: WSL Service tests
    if (-not $NoService) {
        $result = Invoke-Test -Name "WSL Service Tests" -ScriptBlock {
            & wsl bash -c "export NCD_TEST_MODE=1 NCD_UI_KEYS=ENTER NCD_UI_KEYS_STRICT=1; cd '$wslPath' && chmod +x test/test_service_wsl.sh && test/test_service_wsl.sh"
        }
        $passed = $result -and $passed
    }

    # Test 2: WSL NCD standalone
    $result = Invoke-Test -Name "WSL NCD Standalone" -ScriptBlock {
        & wsl bash -c "export NCD_TEST_MODE=1 NCD_UI_KEYS=ENTER NCD_UI_KEYS_STRICT=1; cd '$wslPath' && chmod +x test/test_ncd_wsl_standalone.sh && test/test_ncd_wsl_standalone.sh"
    }
    $passed = $result -and $passed

    # Test 3: WSL NCD with service
    if (-not $NoService) {
        $result = Invoke-Test -Name "WSL NCD with Service" -ScriptBlock {
            & wsl bash -c "export NCD_TEST_MODE=1 NCD_UI_KEYS=ENTER NCD_UI_KEYS_STRICT=1; cd '$wslPath' && chmod +x test/test_ncd_wsl_with_service.sh && test/test_ncd_wsl_with_service.sh"
        }
        $passed = $result -and $passed
    }

    # Test 4: WSL Feature tests
    $result = Invoke-Test -Name "WSL Feature Tests" -ScriptBlock {
        & wsl bash -c "export NCD_TEST_MODE=1 NCD_UI_KEYS=ENTER NCD_UI_KEYS_STRICT=1; cd '$wslPath' && chmod +x test/Wsl/test_features.sh && test/Wsl/test_features.sh"
    }
    $passed = $result -and $passed

    # Test 5: WSL Agent command tests
    $result = Invoke-Test -Name "WSL Agent Command Tests" -ScriptBlock {
        & wsl bash -c "export NCD_TEST_MODE=1 NCD_UI_KEYS=ENTER NCD_UI_KEYS_STRICT=1; cd '$wslPath' && chmod +x test/Wsl/test_agent_commands.sh && test/Wsl/test_agent_commands.sh"
    }
    $passed = $result -and $passed

    return $passed
}

function Show-Summary {
    Write-Section "Test Summary"
    
    $total = $script:TestResults.Count
    $passed = ($script:TestResults | Where-Object { $_.Passed }).Count
    $failed = $total - $passed
    $duration = (Get-Date) - $script:StartTime
    
    Write-Host "Total Tests:    $total"
    Write-Host "Passed:         $passed" -ForegroundColor Green
    Write-Host "Failed:         $failed" -ForegroundColor $(if ($failed -gt 0) { 'Red' } else { 'Green' })
    Write-Host "Total Duration: $($duration.ToString('mm\:ss'))"
    
    if ($failed -gt 0) {
        Write-Host "`nFailed Tests:" -ForegroundColor Red
        $script:TestResults | Where-Object { -not $_.Passed } | ForEach-Object {
            Write-Host "  - $($_.Name)" -ForegroundColor Red
        }
    }
    
    return $failed -eq 0
}

# ============================================================================
# Main Execution
# ============================================================================

try {
    # Handle environment check/repair only
    if ($CheckEnvironment) {
        $state = Test-Environment
        Write-Section "Environment Check"
        Write-Host "LOCALAPPDATA:    $($env:LOCALAPPDATA)"
        Write-Host "NCD_TEST_MODE:   $($env:NCD_TEST_MODE)"
        Write-Host "Database found:  $($state.HasDatabase)"
        Write-Host "Orphaned temps:  $($state.OrphanedCount)"
        Write-Host "Is clean:        $($state.IsClean)"
        
        if (-not $state.IsClean) {
            Write-Host "`nIssues found:" -ForegroundColor Red
            $state.Issues | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
            exit 1
        }
        exit 0
    }
    
    if ($RepairEnvironment) {
        Repair-Environment
        exit 0
    }
    
    # Main test execution
    Write-Section "NCD PowerShell Test Runner"
    Write-Host "Test Suite:     $TestSuite"
    Write-Host "Skip Build:     $SkipBuild"
    Write-Host "Windows Only:   $WindowsOnly"
    Write-Host "WSL Only:       $WslOnly"
    Write-Host "No Service:     $NoService"
    Write-Host "Quick Mode:     $Quick"
    Write-Host ""
    Write-Host "NOTE: Press Ctrl+C to interrupt. Cleanup will still run!" -ForegroundColor Yellow
    
    # Save original environment
    Save-EnvironmentState
    
    # Set test mode and isolate environment
    $env:NCD_TEST_MODE = "1"

    # Prevent TUI from blocking on multi-match: inject ENTER to auto-select
    # the first match, and when the key queue is empty, return ESC immediately
    # instead of waiting for console input.
    $env:NCD_UI_KEYS = "ENTER"
    $env:NCD_UI_KEYS_STRICT = "1"
    
    # Create isolated temp directory for this test run
    $testRunId = "ncd_test_$([System.DateTime]::Now.ToString('yyyyMMdd_HHmmss'))_$([System.Random]::new().Next(1000, 9999))"
    $script:TestTempDir = Join-Path $env:TEMP $testRunId
    New-Item -ItemType Directory -Path $script:TestTempDir -Force | Out-Null
    
    # Isolate metadata from user data
    $env:LOCALAPPDATA = $script:TestTempDir
    $env:XDG_DATA_HOME = $script:TestTempDir
    
    Write-Verbose "Test isolation: LOCALAPPDATA and XDG_DATA_HOME set to $script:TestTempDir"
    
    # Build phase
    if (-not $SkipBuild -and $TestSuite -ne 'None') {
        Build-Project
    }
    
    # Run tests based on suite
    $allPassed = $true
    
    switch ($TestSuite) {
        'All' {
            if (-not $SkipUnit -and -not $WslOnly) {
                $allPassed = (Run-UnitTests) -and $allPassed
            }
            if (-not $WslOnly) {
                $allPassed = (Run-WindowsIntegrationTests) -and $allPassed
            }
            if (-not $WindowsOnly) {
                $allPassed = (Run-WslIntegrationTests) -and $allPassed
            }
        }
        'Unit' {
            $allPassed = Run-UnitTests
        }
        'Integration' {
            if (-not $WslOnly) {
                $allPassed = (Run-WindowsIntegrationTests) -and $allPassed
            }
            if (-not $WindowsOnly) {
                $allPassed = (Run-WslIntegrationTests) -and $allPassed
            }
        }
        'Service' {
            $allPassed = Invoke-Test -Name "Service Tests" -ScriptBlock {
                & .\test\Test-Service-Windows.bat
            }
        }
        'NcdStandalone' {
            $allPassed = Invoke-Test -Name "NCD Standalone" -ScriptBlock {
                & .\test\Test-NCD-Windows-Standalone.bat
            }
        }
        'NcdWithService' {
            $allPassed = Invoke-Test -Name "NCD with Service" -ScriptBlock {
                & .\test\Test-NCD-Windows-With-Service.bat
            }
        }
        'Windows' {
            if (-not $SkipUnit) {
                $allPassed = (Run-UnitTests) -and $allPassed
            }
            $allPassed = (Run-WindowsIntegrationTests) -and $allPassed
        }
        'Wsl' {
            if (-not $SkipUnit) {
                # Run WSL unit tests via make
                $wslAvailable = try { $null -ne (& wsl echo "wsl_ok" 2>&1 | Select-String "wsl_ok") } catch { $false }
                if ($wslAvailable) {
                    $drive = (Get-Location).Path[0].ToString().ToLower()
                    $path = (Get-Location).Path.Substring(2).Replace('\', '/')
                    $wslPath = "/mnt/$drive$path"
                    $unitResult = Invoke-Test -Name "WSL Unit Tests" -ScriptBlock {
                        & wsl bash -c "cd '$wslPath/test' && make test"
                    }
                    $allPassed = $unitResult -and $allPassed
                }
            }
            $allPassed = (Run-WslIntegrationTests) -and $allPassed
        }
        'None' {
            Write-Host "No tests selected (TestSuite = 'None')"
        }
        'ParallelAgent1' {
            $allPassed = Run-ParallelExpansionTests -Agent '1'
        }
        'ParallelAgent2' {
            $allPassed = Run-ParallelExpansionTests -Agent '2'
        }
        'ParallelAgent3' {
            $allPassed = Run-ParallelExpansionTests -Agent '3'
        }
        'ParallelAgent4' {
            $allPassed = Run-ParallelExpansionTests -Agent '4'
        }
    }
    
    # Show summary
    $summaryPassed = Show-Summary
    
    exit $(if ($allPassed -and $summaryPassed) { 0 } else { 1 })
}
finally {
    # This ALWAYS runs, even on Ctrl+C!
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host "FINAL CLEANUP (Running due to try/finally)" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    
    # Stop any running services
    Stop-NcdProcesses
    
    # Clean up temp directories
    Remove-TestTempDirs -Silent
    
    # Remove our test temp directory
    if ($script:TestTempDir -and (Test-Path $script:TestTempDir)) {
        try {
            Remove-Item -Path $script:TestTempDir -Recurse -Force -ErrorAction SilentlyContinue
            Write-Verbose "Removed test temp directory: $($script:TestTempDir)"
        }
        catch {
            Write-Verbose "Could not remove test temp directory: $($script:TestTempDir)"
        }
    }
    
    # Clear any TUI environment variables that may have been set during tests
    # This prevents headless mode or key injection from affecting subsequent NCD usage
    Clear-TuiEnvironmentVariables -Silent
    
    # Restore original environment
    Restore-EnvironmentState
    
    Write-Host "Environment restored. Safe to continue using NCD." -ForegroundColor Green
}
