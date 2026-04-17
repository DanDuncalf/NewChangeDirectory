# ==========================================================================
# run_all_tests.ps1 -- Comprehensive NCD Test Runner (All 4 Environments)
# ==========================================================================
#
# This script runs the complete NCD test suite across all four testing
# environments:
#
#   1. Windows WITHOUT service (standalone mode)
#   2. Windows WITH service (shared memory mode)
#   3. WSL WITHOUT service (standalone mode)
#   4. WSL WITH service (shared memory mode)
#
# USAGE:
#   cd test
#   powershell -ExecutionPolicy Bypass -File run_all_tests.ps1
#
# REQUIREMENTS:
#   - Windows: NCD binaries built (NewChangeDirectory.exe, NCDService.exe)
#   - WSL: NCD binary built in WSL environment
#   - PowerShell 5.0 or later
# ==========================================================================

[CmdletBinding()]
param(
    [switch]$WindowsOnly,
    [switch]$WslOnly,
    [switch]$NoService,
    [switch]$SkipUnitTests,
    [switch]$SkipFeatureTests,
    [switch]$Help
)

if ($Help) {
    Write-Host "NCD Comprehensive Test Runner"
    Write-Host ""
    Write-Host "Usage: run_all_tests.ps1 [options]"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -WindowsOnly       Run only Windows tests"
    Write-Host "  -WslOnly           Run only WSL tests"
    Write-Host "  -NoService         Skip tests with service running"
    Write-Host "  -SkipUnitTests     Skip unit tests"
    Write-Host "  -SkipFeatureTests  Skip feature tests"
    Write-Host "  -Help              Show this help message"
    Write-Host ""
    Write-Host "This script runs tests across 4 environments:"
    Write-Host "  1. Windows without service"
    Write-Host "  2. Windows with service"
    Write-Host "  3. WSL without service"
    Write-Host "  4. WSL with service"
    exit 0
}

$ErrorActionPreference = "Stop"

# Disable NCD background rescans during testing to prevent scanning user drives
$env:NCD_TEST_MODE = "1"

$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$PROJECT_ROOT = Split-Path -Parent $SCRIPT_DIR

$script:Results = @{
    TotalRuns = 0
    Passed = 0
    Failed = 0
    Skipped = 0
    Details = @()
}

function Write-Header($text) {
    Write-Host ""
    Write-Host "========================================"
    Write-Host $text
    Write-Host "========================================"
}

function Write-Section($text) {
    Write-Host ""
    Write-Host $text
}

function Test-WindowsBinaries {
    $exe = Join-Path $PROJECT_ROOT "NewChangeDirectory.exe"
    if (-not (Test-Path $exe)) {
        Write-Host "ERROR: NewChangeDirectory.exe not found. Build with: build.bat" -ForegroundColor Red
        return $false
    }
    return $true
}

function Test-WslAvailable {
    $wslCheck = & wsl --status 2>&1
    if ($LASTEXITCODE -eq 0) {
        return $true
    }
    Write-Host "WARNING: WSL is not available or not configured" -ForegroundColor Yellow
    return $false
}

function Stop-NcdServiceWsl {
    $null = & wsl pkill -f ncd_service 2>$null
    Start-Sleep -Milliseconds 500
}

function Stop-NcdServiceWindows {
    $serviceBat = Join-Path $PROJECT_ROOT "ncd_service.bat"
    if (Test-Path $serviceBat) {
        $cmdLine = '"' + $serviceBat + '" stop 2>nul'
        $null = & cmd /c $cmdLine 2>&1 | Out-Null
    }
    $null = Stop-Process -Name "NCDService" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

function Start-NcdServiceWsl {
    $driveLetter = $PROJECT_ROOT[0].ToString().ToLower()
    $pathPart = $PROJECT_ROOT.Substring(2).Replace('\', '/')
    $projPath = "/mnt/$driveLetter$pathPart"
    $wslCmd = 'cd ' + $projPath + ' ; ./ncd_service start 2>/dev/null'
    $null = & wsl bash -c $wslCmd 2>&1
    Start-Sleep -Seconds 2
}

function Start-NcdServiceWindows {
    $serviceBat = Join-Path $PROJECT_ROOT "ncd_service.bat"
    if (Test-Path $serviceBat) {
        $cmdLine = 'start /B "' + $serviceBat + '" start'
        $null = & cmd /c $cmdLine 2>&1
    }
    Start-Sleep -Seconds 2
}

function Test-ServiceStatusWsl($ExpectRunning) {
    $driveLetter = $PROJECT_ROOT[0].ToString().ToLower()
    $pathPart = $PROJECT_ROOT.Substring(2).Replace('\', '/')
    $projPath = "/mnt/$driveLetter$pathPart"
    $wslCmd = 'cd ' + $projPath + ' ; ./NewChangeDirectory --agent:check --service-status 2>&1'
    $status = & wsl bash -c $wslCmd 2>&1
    $isRunning = ($status -match "READY") -or ($status -match "STARTING") -or ($status -match "LOADING")
    
    if ($ExpectRunning) { return $isRunning } else { return -not $isRunning }
}

function Test-ServiceStatusWindows($ExpectRunning) {
    $exe = Join-Path $PROJECT_ROOT "NewChangeDirectory.exe"
    $status = & $exe /agent check --service-status 2>&1
    $isRunning = ($status -match "READY") -or ($status -match "STARTING") -or ($status -match "LOADING")
    
    if ($ExpectRunning) { return $isRunning } else { return -not $isRunning }
}

function Run-TestEnvironment($Platform, $WithService) {
    $name = $Platform
    if ($WithService) { $name = $name + " + Service" } else { $name = $name + " (Standalone)" }
    
    Write-Header ("TESTING: " + $name)
    Write-Host ("Platform: " + $Platform)
    Write-Host ("Service: " + $(if ($WithService) { "ENABLED" } else { "DISABLED" }))
    Write-Host ""
    
    $script:Results.TotalRuns++
    $stopWatch = [System.Diagnostics.Stopwatch]::StartNew()
    $result = "PENDING"
    $reason = ""
    
    try {
        # Stop service first
        if ($Platform -eq "WSL") { Stop-NcdServiceWsl } else { Stop-NcdServiceWindows }
        
        if ($WithService) {
            if ($Platform -eq "WSL") { Start-NcdServiceWsl } else { Start-NcdServiceWindows }
            Start-Sleep -Seconds 1
            $isRunning = if ($Platform -eq "WSL") { Test-ServiceStatusWsl $true } else { Test-ServiceStatusWindows $true }
            
            if (-not $isRunning) {
                Write-Host "WARNING: Service failed to start, skipping test" -ForegroundColor Yellow
                $reason = "Service failed to start"
                $result = "SKIPPED"
                $script:Results.Skipped++
            }
        }
        
        if ($result -ne "SKIPPED") {
            # Run tests
            $exitCode = 0
            
            if ($Platform -eq "WSL") {
                $driveLetter = $PROJECT_ROOT[0].ToString().ToLower()
                $pathPart = $PROJECT_ROOT.Substring(2).Replace('\', '/')
                $projPath = "/mnt/$driveLetter$pathPart"
                $testPath = "$projPath/test"
                
                if (-not $SkipFeatureTests) {
                    Write-Section "Running WSL Feature Tests..."
                    $wslCmd = 'cd ' + $projPath + ' ; bash test/Wsl/test_features.sh'
                    $null = & wsl bash -c $wslCmd 2>&1
                    if ($LASTEXITCODE -ne 0) { $exitCode = 1 }
                    
                    Write-Section "Running WSL Agent Command Tests..."
                    $wslCmd = 'cd ' + $projPath + ' ; chmod +x test/Wsl/test_agent_commands.sh ; bash test/Wsl/test_agent_commands.sh'
                    $null = & wsl bash -c $wslCmd 2>&1
                    if ($LASTEXITCODE -ne 0) { $exitCode = 1 }
                }
                
                if ($exitCode -eq 0 -and -not $SkipUnitTests) {
                    Write-Section "Running WSL Unit Tests..."
                    $wslCmd = 'cd ' + $testPath + ' ; make test'
                    $null = & wsl bash -c $wslCmd 2>&1
                    if ($LASTEXITCODE -ne 0) { $exitCode = 1 }
                }
                
                if ($exitCode -eq 0 -and -not $SkipUnitTests -and $WithService) {
                    Write-Section "Running WSL Service Tests..."
                    $wslCmd = 'cd ' + $testPath + ' ; make service-test'
                    $null = & wsl bash -c $wslCmd 2>&1
                    if ($LASTEXITCODE -ne 0) { $exitCode = 1 }
                }
            } else {
                $featureTest = Join-Path $SCRIPT_DIR "Win\test_features.bat"
                $unitTest = Join-Path $SCRIPT_DIR "build-and-run-tests.bat"
                
                if (-not $SkipFeatureTests -and (Test-Path $featureTest)) {
                    Write-Section "Running Windows Feature Tests..."
                    Push-Location $PROJECT_ROOT
                    $env:LOCALAPPDATA = $env:TEMP
                    $null = & cmd /c $featureTest 2>&1
                    if ($LASTEXITCODE -ne 0) { $exitCode = 1 }
                    Pop-Location
                    
                    $agentTest = Join-Path $SCRIPT_DIR "Win\test_agent_commands.bat"
                    if (Test-Path $agentTest) {
                        Write-Section "Running Windows Agent Command Tests..."
                        Push-Location $PROJECT_ROOT
                        $env:LOCALAPPDATA = $env:TEMP
                        $null = & cmd /c $agentTest 2>&1
                        if ($LASTEXITCODE -ne 0) { $exitCode = 1 }
                        Pop-Location
                    }
                }
                
                if ($exitCode -eq 0 -and -not $SkipUnitTests -and (Test-Path $unitTest)) {
                    Write-Section "Running Windows Unit Tests..."
                    Push-Location $SCRIPT_DIR
                    $null = & cmd /c $unitTest 2>&1
                    if ($LASTEXITCODE -ne 0) { $exitCode = 1 }
                    Pop-Location
                }
            }
            
            if ($exitCode -eq 0) {
                Write-Host ("[PASS] All tests passed for " + $name) -ForegroundColor Green
                $result = "PASSED"
                $script:Results.Passed++
            } else {
                Write-Host ("[FAIL] Tests failed for " + $name) -ForegroundColor Red
                $result = "FAILED"
                $script:Results.Failed++
            }
        }
    }
    catch {
        Write-Host ("[ERROR] Error running " + $name + ": " + $_) -ForegroundColor Red
        $result = "ERROR"
        $reason = $_.ToString()
        $script:Results.Failed++
    }
    finally {
        $stopWatch.Stop()
        if ($Platform -eq "WSL") { Stop-NcdServiceWsl } else { Stop-NcdServiceWindows }
        
        $script:Results.Details += @{
            Name = $name
            Platform = $Platform
            Service = $WithService
            Result = $result
            Reason = $reason
            Duration = $stopWatch.Elapsed
        }
    }
}

function Show-Summary {
    Write-Header "TEST SUMMARY"
    
    Write-Host ""
    Write-Host ("Total Test Runs: " + $script:Results.TotalRuns)
    Write-Host ("Passed:  " + $script:Results.Passed) -ForegroundColor Green
    Write-Host ("Failed:  " + $script:Results.Failed) -ForegroundColor Red
    Write-Host ("Skipped: " + $script:Results.Skipped) -ForegroundColor Yellow
    
    Write-Host ""
    Write-Host "Details:"
    Write-Host "----------------------------------------"
    
    foreach ($detail in $script:Results.Details) {
        $color = switch ($detail.Result) {
            "PASSED" { "Green" }
            "FAILED" { "Red" }
            "SKIPPED" { "Yellow" }
            default { "Yellow" }
        }
        $durationSec = [math]::Round($detail.Duration.TotalSeconds, 1)
        $line = $detail.Result + " - " + $detail.Name + " (" + $durationSec + "s)"
        Write-Host $line -ForegroundColor $color
        
        if ($detail.Reason) {
            Write-Host ("       Reason: " + $detail.Reason)
        }
    }
    
    Write-Host ""
    Write-Host "----------------------------------------"
    
    if ($script:Results.Failed -gt 0) {
        Write-Host "SOME TESTS FAILED" -ForegroundColor Red
        return 1
    } elseif ($script:Results.Passed -eq 0 -and $script:Results.Skipped -gt 0) {
        Write-Host "All tests were skipped" -ForegroundColor Yellow
        return 2
    } else {
        Write-Host "ALL TESTS PASSED" -ForegroundColor Green
        return 0
    }
}

# ==========================================================================
# Main Script
# ==========================================================================

Write-Header "NCD Comprehensive Test Runner"
Write-Host "This will run all NCD tests across 4 environments:"
Write-Host "  1. Windows without service"
Write-Host "  2. Windows with service"
Write-Host "  3. WSL without service"
Write-Host "  4. WSL with service"
Write-Host ""

$testWindows = -not $WslOnly
$testWsl = -not $WindowsOnly
$testWithService = -not $NoService

$canTestWindows = Test-WindowsBinaries
$canTestWsl = Test-WslAvailable

if (-not $canTestWindows -and -not $canTestWsl) {
    Write-Host "ERROR: No testable environments found. Build NCD first." -ForegroundColor Red
    exit 2
}

if ($testWindows -and $canTestWindows) {
    Run-TestEnvironment -Platform "Windows" -WithService $false
    if ($testWithService) {
        Run-TestEnvironment -Platform "Windows" -WithService $true
    }
}

if ($testWsl -and $canTestWsl) {
    Run-TestEnvironment -Platform "WSL" -WithService $false
    if ($testWithService) {
        Run-TestEnvironment -Platform "WSL" -WithService $true
    }
}

$exitCode = Show-Summary
exit $exitCode
