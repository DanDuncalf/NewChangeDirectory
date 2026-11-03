# NcdIntegration.psm1 -- shared helpers for the Windows integration suites
#
# Used by test_service_windows.ps1, test_ncd_windows_standalone.ps1 and
# test_ncd_windows_with_service.ps1. These run through the test harness
# (python test\runner.py integration / python test\generate_report.py).
#
# Isolation: every suite gets its own LOCALAPPDATA and directory tree under
# %TEMP% and runs with NCD_TEST_MODE=1, which forces user mode (per-user pipe,
# mutex and data folder), so the suites never read or write C:\ProgramData\NCD.
#
# Note: NCDService refuses to start a test instance while ANY NCDService is
# running (it checks both the per-user and the Global\ mutex), so a live
# system-wide service must be stopped first. The harness force-kills
# NCDService.exe before each suite.

Set-StrictMode -Version 2.0

$script:ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$script:NcdExe      = Join-Path $script:ProjectRoot "NewChangeDirectory.exe"
$script:ServiceExe  = Join-Path $script:ProjectRoot "NCDService.exe"
$script:TestDir     = Join-Path $script:ProjectRoot "test"

$script:TestsRun = 0
$script:TestsPassed = 0
$script:TestsFailed = 0
$script:TestData = $null
$script:TestRoot = $null

function Get-NcdPaths {
    [pscustomobject]@{
        ProjectRoot = $script:ProjectRoot
        Ncd         = $script:NcdExe
        Service     = $script:ServiceExe
        TestDir     = $script:TestDir
        TestData    = $script:TestData
        DataDir     = if ($script:TestData) { Join-Path $script:TestData "NCD" } else { $null }
        TestRoot    = $script:TestRoot
        ServiceLog  = if ($script:TestData) { Join-Path $script:TestData "NCD\ncd_service.log" } else { $null }
    }
}

function Get-NcdAppVersion {
    $hdr = Get-Content (Join-Path $script:ProjectRoot "src\control_ipc.h") -Raw
    if ($hdr -match '#define\s+NCD_APP_VERSION\s+"([^"]+)"') { return $Matches[1] }
    throw "NCD_APP_VERSION not found in src\control_ipc.h"
}

# --------------------------------------------------------------------- setup

function Initialize-NcdTestEnvironment {
    param([string]$Name)

    foreach ($exe in @($script:NcdExe, $script:ServiceExe)) {
        if (-not (Test-Path $exe)) { throw "Executable not found: $exe (run build.bat)" }
    }

    $id = Get-Random
    $script:TestData = Join-Path $env:TEMP "ncd_${Name}_data_$id"
    $script:TestRoot = Join-Path $env:TEMP "ncd_${Name}_tree_$id"

    $dataDir = Join-Path $script:TestData "NCD"
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    foreach ($d in @("Projects\alpha\src", "Projects\beta\src", "Users\scott\Downloads",
                     "Users\scott\Documents", "Data\Files")) {
        New-Item -ItemType Directory -Force -Path (Join-Path $script:TestRoot $d) | Out-Null
    }

    # Empty metadata container (magic NCMD, version 1) so the service skips
    # its first-run interactive configuration.
    [IO.File]::WriteAllBytes((Join-Path $dataDir "ncd.metadata"),
        [byte[]](0x4E,0x43,0x4D,0x44,0x01,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0))

    # Child processes inherit these.
    $env:LOCALAPPDATA = $script:TestData
    $env:NCD_TEST_MODE = "1"
    Remove-Item Env:\NCD_UI_KEYS -ErrorAction SilentlyContinue

    Write-Host "NCD:       $script:NcdExe"
    Write-Host "Service:   $script:ServiceExe"
    Write-Host "Test Data: $script:TestData"
    Write-Host "Test Root: $script:TestRoot"
    Write-Host ""
}

function Remove-NcdTestEnvironment {
    Stop-NcdTestService -Quiet
    foreach ($p in @($script:TestData, $script:TestRoot)) {
        if ($p -and (Test-Path $p)) { Remove-Item -Recurse -Force $p -ErrorAction SilentlyContinue }
    }
}

# ------------------------------------------------------------------ commands

function Format-NcdArg([string]$a) {
    if ($a -eq "") { return '""' }
    if ($a -match '[\s"]') { return '"' + ($a -replace '"', '\"') + '"' }
    return $a
}

# Run an executable with a timeout; returns ExitCode, Output (stdout+stderr), StdOut, StdErr.
function Invoke-NcdProcess {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        [int]$TimeoutSeconds = 30,
        [string]$WorkingDirectory = $script:TestRoot,
        [hashtable]$Environment = @{}
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = (($Arguments | ForEach-Object { Format-NcdArg $_ }) -join " ")
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardInput = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    if ($WorkingDirectory) { $psi.WorkingDirectory = $WorkingDirectory }
    foreach ($k in $Environment.Keys) { $psi.EnvironmentVariables[$k] = [string]$Environment[$k] }

    $proc = [System.Diagnostics.Process]::Start($psi)
    $proc.StandardInput.Close()
    $outTask = $proc.StandardOutput.ReadToEndAsync()
    $errTask = $proc.StandardError.ReadToEndAsync()
    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
        try { $proc.Kill() } catch { }
        $proc.WaitForExit(2000) | Out-Null
        return [pscustomobject]@{ ExitCode = 124; Output = "TIMEOUT after ${TimeoutSeconds}s"; StdOut = ""; StdErr = "" }
    }
    $proc.WaitForExit()
    $out = $outTask.Result
    $err = $errTask.Result
    [pscustomobject]@{ ExitCode = $proc.ExitCode; Output = ($out + $err).Trim(); StdOut = $out.Trim(); StdErr = $err.Trim() }
}

function Invoke-Ncd {
    param([string[]]$Arguments, [int]$TimeoutSeconds = 30, [string]$WorkingDirectory = $script:TestRoot,
          [hashtable]$Environment = @{})
    Invoke-NcdProcess -FilePath $script:NcdExe -Arguments $Arguments -TimeoutSeconds $TimeoutSeconds `
        -WorkingDirectory $WorkingDirectory -Environment $Environment
}

# Plain (non-agent) search, as the ncd.bat wrapper runs it. The exe writes its
# answer to %TEMP%\ncd_result.bat; TEMP is pointed at a private folder so the
# result can be read back. Returns the search result plus ResultFile text.
function Invoke-NcdSearch {
    param([string]$Search, [int]$TimeoutSeconds = 30)
    $tmp = Join-Path $script:TestData "result_tmp"
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    $resultFile = Join-Path $tmp "ncd_result.bat"
    Remove-Item $resultFile -Force -ErrorAction SilentlyContinue
    $r = Invoke-Ncd -Arguments @($Search) -TimeoutSeconds $TimeoutSeconds -Environment @{ TEMP = $tmp; TMP = $tmp }
    $text = if (Test-Path $resultFile) { Get-Content $resultFile -Raw } else { "" }
    $r | Add-Member -NotePropertyName ResultFile -NotePropertyValue $text -PassThru
}

function Invoke-NcdService {
    param([string[]]$Arguments, [int]$TimeoutSeconds = 30)
    Invoke-NcdProcess -FilePath $script:ServiceExe -Arguments $Arguments -TimeoutSeconds $TimeoutSeconds -WorkingDirectory $script:ProjectRoot
}

function Invoke-IpcTool {
    param([string]$Tool, [string[]]$Arguments, [int]$TimeoutSeconds = 30)
    $exe = Join-Path $script:TestDir "$Tool.exe"
    if (-not (Test-Path $exe)) { throw "IPC tool not found: $exe (run test\build-tests.bat)" }
    Invoke-NcdProcess -FilePath $exe -Arguments $Arguments -TimeoutSeconds $TimeoutSeconds -WorkingDirectory $script:ProjectRoot
}

# ------------------------------------------------------------------- service

# Service processes started from this build (never the deployed copy).
function Get-NcdTestServiceProcess {
    @(Get-Process -Name NCDService -ErrorAction SilentlyContinue | Where-Object {
        try { $_.Path -and ([IO.Path]::GetFullPath($_.Path) -ieq [IO.Path]::GetFullPath($script:ServiceExe)) } catch { $false }
    })
}

# Parse the JSON object from tool output that may start with a banner.
function ConvertFrom-NcdJsonOutput([string]$Text) {
    $a = $Text.IndexOf("{"); $b = $Text.LastIndexOf("}")
    if ($a -lt 0 -or $b -lt $a) { throw "no JSON object in output: $Text" }
    $Text.Substring($a, $b - $a + 1) | ConvertFrom-Json
}

function Get-NcdServiceStatus {
    # First line only: "NOT_RUNNING", "STARTING" or "State: READY" followed by details.
    $r = Invoke-Ncd -Arguments @("--agent:check", "--service-status") -TimeoutSeconds 15
    $first = ($r.StdOut -split "`r?`n")[0].Trim()
    return ($first -replace '^State:\s*', '')
}

function Wait-NcdServiceReady {
    param([int]$TimeoutSeconds = 20)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ((Get-NcdServiceStatus) -match '^READY') { return $true }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Wait-NcdServiceGone {
    param([int]$TimeoutSeconds = 10)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (@(Get-NcdTestServiceProcess).Count -eq 0) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

# Start the service (detached daemon) and wait for READY. Returns the start command result.
function Start-NcdTestService {
    param([int]$TimeoutSeconds = 20, [switch]$NoWait)
    $r = Invoke-NcdService -Arguments @("start", "-log2") -TimeoutSeconds 30
    if (-not $NoWait) { $r | Add-Member -NotePropertyName Ready -NotePropertyValue (Wait-NcdServiceReady -TimeoutSeconds $TimeoutSeconds) }
    return $r
}

# Graceful stop; force-kills only this build's service if it does not exit.
function Stop-NcdTestService {
    param([switch]$Quiet)
    $r = Invoke-NcdService -Arguments @("stop") -TimeoutSeconds 20
    if (-not (Wait-NcdServiceGone -TimeoutSeconds 10)) {
        if (-not $Quiet) { Write-Host "  [INFO] Service did not exit after stop; force-killing" }
        Get-NcdTestServiceProcess | Stop-Process -Force -ErrorAction SilentlyContinue
        Wait-NcdServiceGone -TimeoutSeconds 5 | Out-Null
    }
    return $r
}

function Get-NcdServiceLogErrors {
    $log = (Get-NcdPaths).ServiceLog
    if (-not (Test-Path $log)) { return @() }
    # Same rule as the Linux suites: an upper-case ERROR on a log line.
    @(Select-String -Path $log -Pattern '\[L[0-9]+\] .*ERROR' -CaseSensitive | ForEach-Object { $_.Line })
}

# ------------------------------------------------------------------ results

function Write-NcdHeader([string]$Title) {
    Write-Host ""
    Write-Host "========================================"
    Write-Host $Title
    Write-Host "========================================"
}

function Write-NcdTestStart([string]$Label) { Write-Host "[TEST] $Label" }

function Add-NcdResult {
    param([string]$Name, [bool]$Passed, [string]$Reason = "")
    $script:TestsRun++
    if ($Passed) {
        $script:TestsPassed++
        Write-Host "  [PASS] $Name"
    } else {
        $script:TestsFailed++
        Write-Host "  [FAIL] $Name"
        if ($Reason) {
            $one = ($Reason -replace "`r?`n", " | ")
            if ($one.Length -gt 400) { $one = $one.Substring(0, 400) + "..." }
            Write-Host "         Reason: $one"
        }
    }
}

# Run a scriptblock as one test. The block returns $true/$false, or a string
# (failure reason). Exceptions count as failures.
function Invoke-NcdTest {
    param([string]$Name, [scriptblock]$Body)
    Write-NcdTestStart $Name
    try {
        $res = & $Body
        if ($res -is [array]) { $res = $res[-1] }
        if ($res -is [string]) { Add-NcdResult $Name $false $res }
        else { Add-NcdResult $Name ([bool]$res) "assertion returned false" }
    } catch {
        Add-NcdResult $Name $false "exception: $($_.Exception.Message)"
    }
}

# Agent writes (mkdir/mv/rmdir) followed by agent reads of the same paths.
# Shared by the standalone and with-service suites: both modes must agree.
# The parent directory is created behind NCD's back, so it is not indexed:
# the write must add the missing ancestor rather than file the leaf at the
# drive root.
function Invoke-NcdAgentWriteReadTests {
    param([Parameter(Mandatory)][string]$Mode)
    $parent = Join-Path $script:TestRoot "Projects\unindexed_$Mode"
    $leafA  = Join-Path $parent "agentdir_a_$Mode"
    $leafB  = Join-Path $parent "agentdir_b_$Mode"
    Remove-Item $parent -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    try {
        Invoke-NcdTest "Agent mkdir under an unindexed parent is found by query ($Mode)" {
            $m = Invoke-Ncd -Arguments @("--agent:mkdir", $leafA, "--json")
            if ($m.ExitCode -ne 0) { return "mkdir exit $($m.ExitCode): $($m.Output)" }
            $q = Invoke-Ncd -Arguments @("--agent:query", "agentdir_a_$Mode", "--limit", "1")
            if ($q.StdOut -ine $leafA) { return "query got '$($q.Output)', expected '$leafA'" }
            $true
        }

        Invoke-NcdTest "Agent mv: query finds the new path, not the old ($Mode)" {
            $m = Invoke-Ncd -Arguments @("--agent:mv", $leafA, $leafB, "--json")
            if ($m.ExitCode -ne 0) { return "mv exit $($m.ExitCode): $($m.Output)" }
            $q = Invoke-Ncd -Arguments @("--agent:query", "agentdir_b_$Mode", "--limit", "1")
            if ($q.StdOut -ine $leafB) { return "query new got '$($q.Output)', expected '$leafB'" }
            $old = Invoke-Ncd -Arguments @("--agent:query", "agentdir_a_$Mode")
            if ($old.ExitCode -eq 0) { return "old path still indexed: $($old.Output)" }
            $true
        }

        Invoke-NcdTest "Agent check and complete see the moved path ($Mode)" {
            $c = Invoke-Ncd -Arguments @("--agent:check", $leafB, "--json")
            $j = $c.StdOut | ConvertFrom-Json
            if (-not $j.in_db) { return "check in_db false: $($c.StdOut)" }
            $k = Invoke-Ncd -Arguments @("--agent:complete", "agentdir_b_")
            if ($k.Output -notmatch "agentdir_b_$Mode") { return "complete: '$($k.Output)'" }
            $true
        }

        Invoke-NcdTest "Agent rmdir: query no longer finds it ($Mode)" {
            $m = Invoke-Ncd -Arguments @("--agent:rmdir", $leafB, "--json")
            if ($m.ExitCode -ne 0) { return "rmdir exit $($m.ExitCode): $($m.Output)" }
            $q = Invoke-Ncd -Arguments @("--agent:query", "agentdir_b_$Mode")
            if ($q.ExitCode -eq 0) { return "removed path still indexed: $($q.Output)" }
            $true
        }
    } finally {
        Remove-Item $parent -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Complete-NcdSuite {
    Write-NcdHeader "Test Summary"
    Write-Host "  Total:   $script:TestsRun"
    Write-Host "  Passed:  $script:TestsPassed"
    Write-Host "  Failed:  $script:TestsFailed"
    Write-Host "  Skipped: 0"
    Write-Host ""
    if ($script:TestsFailed -gt 0 -or $script:TestsRun -eq 0) {
        Write-Host "RESULT: FAILED"
        return 1
    }
    Write-Host "RESULT: PASSED"
    return 0
}

Export-ModuleMember -Function Get-NcdPaths, Get-NcdAppVersion, Initialize-NcdTestEnvironment,
    Remove-NcdTestEnvironment, Invoke-NcdProcess, Invoke-Ncd, Invoke-NcdSearch, Invoke-NcdService, Invoke-IpcTool,
    Get-NcdTestServiceProcess, ConvertFrom-NcdJsonOutput, Get-NcdServiceStatus, Wait-NcdServiceReady, Wait-NcdServiceGone,
    Start-NcdTestService, Stop-NcdTestService, Get-NcdServiceLogErrors, Write-NcdHeader,
    Add-NcdResult, Invoke-NcdTest, Invoke-NcdAgentWriteReadTests, Complete-NcdSuite
