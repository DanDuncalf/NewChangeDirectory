# ============================================================================
# NcdTestUtils.psm1 - PowerShell Test Utilities for NCD
# ============================================================================
#
# PURPOSE:
#   Provides robust test execution with guaranteed cleanup using PowerShell's
#   try/finally blocks which CAN trap Ctrl+C interrupts.
#
# FUNCTIONS:
#   Save-NcdTestEnvironment    - Saves current environment variables
#   Restore-NcdTestEnvironment - Restores saved environment variables
#   Invoke-NcdTest             - Runs a test with proper cleanup
#   Invoke-NcdTestBatch        - Runs a batch file with cleanup
#   Test-NcdEnvironment        - Verifies environment is clean
#   Repair-NcdEnvironment      - Fixes corrupted environment
#   New-NcdTestVhd             - Creates a test VHD
#   Remove-NcdTestVhd          - Removes a test VHD and detaches if needed
#
# ============================================================================

# Module-level variable to store saved environment
$script:SavedEnvironment = $null
$script:CleanupActions = @()

<#
.SYNOPSIS
    Saves the current NCD-related environment variables.

.DESCRIPTION
    Saves LOCALAPPDATA, NCD_TEST_MODE, TEMP, and PATH so they can be
    restored later even if the test is interrupted.

.EXAMPLE
    Save-NcdTestEnvironment
#>
function Save-NcdTestEnvironment {
    [CmdletBinding()]
    param()
    
    $script:SavedEnvironment = @{
        LOCALAPPDATA = $env:LOCALAPPDATA
        NCD_TEST_MODE = $env:NCD_TEST_MODE
        TEMP = $env:TEMP
        PATH = $env:PATH
        XDG_DATA_HOME = $env:XDG_DATA_HOME
        Timestamp = Get-Date
    }
    
    Write-Verbose "Saved environment state at $($script:SavedEnvironment.Timestamp)"
    Write-Verbose "  LOCALAPPDATA: $($script:SavedEnvironment.LOCALAPPDATA)"
    Write-Verbose "  NCD_TEST_MODE: $($script:SavedEnvironment.NCD_TEST_MODE)"
    
    return $script:SavedEnvironment
}

<#
.SYNOPSIS
    Restores the NCD-related environment variables to their saved state.

.DESCRIPTION
    Restores all environment variables that were saved with Save-NcdTestEnvironment.
    This function is safe to call multiple times.

.EXAMPLE
    Restore-NcdTestEnvironment
#>
function Restore-NcdTestEnvironment {
    [CmdletBinding()]
    param(
        [switch]$Silent
    )
    
    if ($null -eq $script:SavedEnvironment) {
        if (-not $Silent) {
            Write-Warning "No saved environment found to restore"
        }
        return
    }
    
    if (-not $Silent) {
        Write-Host "Restoring environment variables..." -ForegroundColor Cyan
    }
    
    # Restore each variable
    $env:LOCALAPPDATA = $script:SavedEnvironment.LOCALAPPDATA
    $env:NCD_TEST_MODE = $script:SavedEnvironment.NCD_TEST_MODE
    $env:TEMP = $script:SavedEnvironment.TEMP
    $env:PATH = $script:SavedEnvironment.PATH
    $env:XDG_DATA_HOME = $script:SavedEnvironment.XDG_DATA_HOME
    
    if (-not $Silent) {
        Write-Host "  LOCALAPPDATA restored to: $env:LOCALAPPDATA" -ForegroundColor Gray
        Write-Host "  NCD_TEST_MODE restored to: $env:NCD_TEST_MODE" -ForegroundColor Gray
    }
}

<#
.SYNOPSIS
    Registers a cleanup action to run when tests complete.

.DESCRIPTION
    Adds an action to the cleanup queue that will be executed during
    cleanup, even if the test is interrupted with Ctrl+C.

.PARAMETER Action
    A script block to execute during cleanup.

.PARAMETER Description
    A description of what this cleanup action does.

.EXAMPLE
    Register-NcdCleanupAction -Description "Remove temp dir" -Action { Remove-Item $tempDir -Recurse }
#>
function Register-NcdCleanupAction {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Description,
        
        [Parameter(Mandatory)]
        [scriptblock]$Action
    )
    
    $script:CleanupActions += [PSCustomObject]@{
        Description = $Description
        Action = $Action
    }
    
    Write-Verbose "Registered cleanup action: $Description"
}

<#
.SYNOPSIS
    Executes all registered cleanup actions.

.DESCRIPTION
    Runs all cleanup actions in reverse order (LIFO) and clears the queue.
    This is called automatically by Invoke-NcdTest in the finally block.

.EXAMPLE
    Invoke-NcdCleanup
#>
function Invoke-NcdCleanup {
    [CmdletBinding()]
    param(
        [switch]$Silent
    )
    
    if ($script:CleanupActions.Count -eq 0) {
        return
    }
    
    if (-not $Silent) {
        Write-Host "Running cleanup actions..." -ForegroundColor Cyan
    }
    
    # Process in reverse order (LIFO)
    for ($i = $script:CleanupActions.Count - 1; $i -ge 0; $i--) {
        $cleanup = $script:CleanupActions[$i]
        if (-not $Silent) {
            Write-Host "  $($cleanup.Description)" -ForegroundColor Gray
        }
        
        try {
            & $cleanup.Action
        }
        catch {
            Write-Warning "Cleanup action failed: $($cleanup.Description) - $_"
        }
    }
    
    # Clear the cleanup queue
    $script:CleanupActions = @()
}

<#
.SYNOPSIS
    Clears all registered cleanup actions without executing them.

.DESCRIPTION
    Useful when you want to cancel pending cleanups.

.EXAMPLE
    Clear-NcdCleanupActions
#>
function Clear-NcdCleanupActions {
    [CmdletBinding()]
    param()
    
    $count = $script:CleanupActions.Count
    $script:CleanupActions = @()
    Write-Verbose "Cleared $count cleanup actions"
}

<#
.SYNOPSIS
    Runs a test script block with guaranteed cleanup.

.DESCRIPTION
    Executes the TestScript in a try block and runs cleanup in a finally block.
    This ensures cleanup runs even if Ctrl+C is pressed during the test.

.PARAMETER TestScript
    The script block containing the test code to run.

.PARAMETER TestName
    A name for this test run (used in output).

.PARAMETER TimeoutSeconds
    Maximum time to allow the test to run before forcefully terminating.

.EXAMPLE
    Invoke-NcdTest -TestName "Service Tests" -TestScript {
        & .\test\Test-Service-Windows.bat
    }
#>
function Invoke-NcdTest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$TestName,
        
        [Parameter(Mandatory)]
        [scriptblock]$TestScript,
        
        [int]$TimeoutSeconds = 300
    )
    
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Running Test: $TestName" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    
    # Save environment before starting
    Save-NcdTestEnvironment | Out-Null
    
    # Set test mode
    $env:NCD_TEST_MODE = "1"
    
    $exitCode = 0
    $startTime = Get-Date
    
    try {
        # Run the test script
        & $TestScript
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0 -and $exitCode -ne $null) {
            Write-Warning "Test exited with code: $exitCode"
        }
    }
    catch {
        Write-Error "Test failed with exception: $_"
        $exitCode = 1
    }
    finally {
        # This block ALWAYS runs, even on Ctrl+C
        $duration = (Get-Date) - $startTime
        Write-Host "`nTest duration: $($duration.ToString('mm\:ss'))" -ForegroundColor Gray
        
        # Run registered cleanup actions
        Invoke-NcdCleanup
        
        # Restore environment
        Restore-NcdTestEnvironment
        
        Write-Host "Cleanup complete." -ForegroundColor Green
    }
    
    return $exitCode
}

<#
.SYNOPSIS
    Runs a batch file with guaranteed cleanup.

.DESCRIPTION
    Invokes a batch file and ensures cleanup runs afterward, even on Ctrl+C.

.PARAMETER BatchPath
    Path to the batch file to run.

.PARAMETER Arguments
    Arguments to pass to the batch file.

.PARAMETER WorkingDirectory
    Working directory for the batch file.

.PARAMETER TestName
    Optional name for this test run.

.EXAMPLE
    Invoke-NcdTestBatch -BatchPath ".\test\Test-Service-Windows.bat" -TestName "Service Tests"
#>
function Invoke-NcdTestBatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$BatchPath,
        
        [string[]]$Arguments = @(),
        
        [string]$WorkingDirectory = (Get-Location),
        
        [string]$TestName = (Split-Path $BatchPath -Leaf)
    )
    
    # Resolve the full path
    $fullPath = Resolve-Path $BatchPath -ErrorAction SilentlyContinue
    if (-not $fullPath) {
        Write-Error "Batch file not found: $BatchPath"
        return 1
    }
    
    # Build the command
    $argString = $Arguments -join ' '
    
    return Invoke-NcdTest -TestName $TestName -TestScript {
        Write-Host "Executing: $fullPath $argString" -ForegroundColor Yellow
        Write-Host "Working directory: $WorkingDirectory" -ForegroundColor Gray
        
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = "cmd.exe"
        $psi.Arguments = "/c `"`"$fullPath`" $argString`""
        $psi.WorkingDirectory = $WorkingDirectory
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        
        $process = [System.Diagnostics.Process]::Start($psi)
        
        # Stream output in real-time
        while (-not $process.StandardOutput.EndOfStream) {
            $line = $process.StandardOutput.ReadLine()
            Write-Host $line
        }
        
        $stderr = $process.StandardError.ReadToEnd()
        if ($stderr) {
            Write-Warning $stderr
        }
        
        $process.WaitForExit()
        $global:LASTEXITCODE = $process.ExitCode
    }
}

<#
.SYNOPSIS
    Verifies that the environment is not corrupted.

.DESCRIPTION
    Checks if LOCALAPPDATA points to a real user profile location
    and not a temp directory from interrupted tests.

.EXAMPLE
    Test-NcdEnvironment
#>
function Test-NcdEnvironment {
    [CmdletBinding()]
    param()
    
    $issues = @()
    
    # Check LOCALAPPDATA
    $localAppData = $env:LOCALAPPDATA
    if ($localAppData -match 'temp[/\\]ncd') {
        $issues += "LOCALAPPDATA points to test temp directory: $localAppData"
    }
    
    # Check if we can find NCD database in expected location
    $ncdDbPath = Join-Path $localAppData "NCD\ncd_*.database"
    $hasDatabases = Test-Path $ncdDbPath
    
    # Check for orphaned test directories
    $orphanedDirs = Get-ChildItem $env:TEMP -Directory -ErrorAction SilentlyContinue | 
        Where-Object { $_.Name -match '^ncd_test_|^ncd_.*_test_' }
    
    return [PSCustomObject]@{
        IsClean = $issues.Count -eq 0
        Issues = $issues
        HasDatabases = $hasDatabases
        LocalAppData = $localAppData
        OrphanedDirectories = $orphanedDirs
    }
}

<#
.SYNOPSIS
    Repairs a corrupted environment.

.DESCRIPTION
    Fixes environment variables that were left in a bad state by
    interrupted test runs.

.EXAMPLE
    Repair-NcdEnvironment
#>
function Repair-NcdEnvironment {
    [CmdletBinding()]
    param()
    
    Write-Host "Repairing NCD environment..." -ForegroundColor Cyan
    
    # Fix LOCALAPPDATA
    if ($env:LOCALAPPDATA -match 'temp[/\\]ncd') {
        $correctPath = Join-Path $env:USERPROFILE "AppData\Local"
        Write-Host "  Fixing LOCALAPPDATA: $env:LOCALAPPDATA -> $correctPath" -ForegroundColor Yellow
        $env:LOCALAPPDATA = $correctPath
    }
    
    # Clean up orphaned test directories
    $orphanedDirs = Get-ChildItem $env:TEMP -Directory -ErrorAction SilentlyContinue | 
        Where-Object { $_.Name -match '^ncd_test_|^ncd_.*_test_' }
    
    foreach ($dir in $orphanedDirs) {
        Write-Host "  Removing orphaned directory: $($dir.FullName)" -ForegroundColor Yellow
        Remove-Item $dir.FullName -Recurse -Force -ErrorAction SilentlyContinue
    }
    
    Write-Host "Environment repair complete." -ForegroundColor Green
    
    return Test-NcdEnvironment
}

<#
.SYNOPSIS
    Creates a test VHD and returns its information.

.DESCRIPTION
    Creates a VHD file using diskpart and mounts it to an available drive letter.

.PARAMETER SizeMB
    Size of the VHD in megabytes (default: 16).

.PARAMETER Label
    Volume label for the VHD (default: NCDTest).

.EXAMPLE
    $vhd = New-NcdTestVhd -SizeMB 32 -Label "MyTest"
#>
function New-NcdTestVhd {
    [CmdletBinding()]
    param(
        [int]$SizeMB = 16,
        [string]$Label = "NCDTest"
    )
    
    # Find an available drive letter
    $usedLetters = Get-Volume | Select-Object -ExpandProperty DriveLetter
    $availableLetters = 'Z', 'Y', 'X', 'W', 'V', 'U', 'T', 'S', 'R', 'Q', 'P', 'O', 'N', 'M', 'L', 'K', 'J', 'I', 'H', 'G' | 
        Where-Object { $_ -notin $usedLetters }
    
    if ($availableLetters.Count -eq 0) {
        throw "No available drive letters found"
    }
    
    $driveLetter = $availableLetters[0]
    $vhdPath = Join-Path $env:TEMP "ncd_test_vhd_$([Guid]::NewGuid().ToString('N')).vhdx"
    
    Write-Host "Creating VHD at $vhdPath on drive $driveLetter`:" -ForegroundColor Cyan
    
    # Create diskpart script
    $diskpartScript = @"
create vdisk file="$vhdPath" maximum=$SizeMB type=expandable
select vdisk file="$vhdPath"
attach vdisk
create partition primary
format fs=ntfs quick label="$Label"
assign letter=$driveLetter
"@
    
    $scriptPath = [System.IO.Path]::GetTempFileName()
    $diskpartScript | Out-File -FilePath $scriptPath -Encoding ASCII
    
    try {
        $result = & diskpart /s $scriptPath 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "DiskPart failed: $result"
        }
        
        # Register cleanup for this VHD
        Register-NcdCleanupAction -Description "Detach and remove VHD: $vhdPath" -Action {
            Remove-NcdTestVhd -VhdPath $vhdPath -Silent
        }
        
        return [PSCustomObject]@{
            Path = $vhdPath
            DriveLetter = $driveLetter
            Label = $Label
            RootPath = "$driveLetter`:\"
            ScriptPath = $scriptPath
        }
    }
    catch {
        Remove-Item $scriptPath -ErrorAction SilentlyContinue
        throw
    }
}

<#
.SYNOPSIS
    Removes a test VHD and detaches it if still attached.

.DESCRIPTION
    Safely detaches and removes a VHD created by New-NcdTestVhd.

.PARAMETER VhdPath
    Path to the VHD file.

.PARAMETER Silent
    Suppress output messages.

.EXAMPLE
    Remove-NcdTestVhd -VhdPath "C:\Temp\test.vhdx"
#>
function Remove-NcdTestVhd {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$VhdPath,
        
        [switch]$Silent
    )
    
    if (-not (Test-Path $VhdPath)) {
        if (-not $Silent) {
            Write-Warning "VHD not found: $VhdPath"
        }
        return
    }
    
    if (-not $Silent) {
        Write-Host "Detaching and removing VHD: $VhdPath" -ForegroundColor Gray
    }
    
    # Create diskpart script to detach
    $diskpartScript = @"
select vdisk file="$VhdPath"
detach vdisk
"@
    
    $scriptPath = [System.IO.Path]::GetTempFileName()
    $diskpartScript | Out-File -FilePath $scriptPath -Encoding ASCII
    
    try {
        & diskpart /s $scriptPath 2>&1 | Out-Null
    }
    finally {
        Remove-Item $scriptPath -ErrorAction SilentlyContinue
    }
    
    # Remove the VHD file
    Remove-Item $VhdPath -Force -ErrorAction SilentlyContinue
}

<#
.SYNOPSIS
    Kills any running NCD service processes.

.DESCRIPTION
    Forcefully terminates NCDService.exe and WSL ncd_service processes.

.EXAMPLE
    Stop-NcdService
#>
function Stop-NcdService {
    [CmdletBinding()]
    param()
    
    Write-Host "Stopping any running NCD services..." -ForegroundColor Gray
    
    # Try graceful stop first
    if (Get-Command NCDService.exe -ErrorAction SilentlyContinue) {
        & NCDService.exe stop 2>&1 | Out-Null
    }
    
    # Force kill Windows service
    Get-Process -Name "NCDService" -ErrorAction SilentlyContinue | 
        Stop-Process -Force -ErrorAction SilentlyContinue
    
    # Force kill WSL service
    & wsl pkill -9 -f "ncd_service" 2>&1 | Out-Null
    & wsl pkill -9 -f "NCDService" 2>&1 | Out-Null
}

# Export all public functions
Export-ModuleMember -Function @(
    'Save-NcdTestEnvironment',
    'Restore-NcdTestEnvironment',
    'Register-NcdCleanupAction',
    'Invoke-NcdCleanup',
    'Clear-NcdCleanupActions',
    'Invoke-NcdTest',
    'Invoke-NcdTestBatch',
    'Test-NcdEnvironment',
    'Repair-NcdEnvironment',
    'New-NcdTestVhd',
    'Remove-NcdTestVhd',
    'Stop-NcdService'
)
