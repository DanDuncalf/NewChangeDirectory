# PowerShell Test Runner for NCD

## Overview

The PowerShell-based test runner solves the fundamental problem with batch files: **they cannot trap Ctrl+C interrupts**. When you press Ctrl+C during a batch file:

1. The batch file terminates immediately
2. No cleanup code runs
3. `endlocal` is never reached
4. Environment variables remain modified (especially `LOCALAPPDATA`)

**PowerShell's `try/finally` blocks CAN trap Ctrl+C**, ensuring cleanup code always runs.

## Files Created

| File | Purpose |
|------|---------|
| `Run-NcdTests.ps1` | Main PowerShell test runner with guaranteed cleanup |
| `Run-Tests-Safe.bat` | Batch wrapper for users who prefer starting from batch |
| `Check-Environment.ps1` | Comprehensive environment diagnostics and repair |
| `test/PowerShell/NcdTestUtils.psm1` | Reusable PowerShell module for test utilities |

## Quick Start

### Recommended: Use the Safe Batch Wrapper

```batch
:: Run all tests (safest option)
Run-Tests-Safe.bat

:: Run only unit tests
Run-Tests-Safe.bat unit

:: Run only integration tests
Run-Tests-Safe.bat integration

:: Check environment (no tests)
Run-Tests-Safe.bat --check

:: Repair corrupted environment
Run-Tests-Safe.bat --repair
```

### PowerShell Native (More Control)

```powershell
# Run all tests
.\Run-NcdTests.ps1

# Run specific test suites
.\Run-NcdTests.ps1 -TestSuite Unit
.\Run-NcdTests.ps1 -TestSuite Integration
.\Run-NcdTests.ps1 -TestSuite Service

# Check/repair environment
.\Run-NcdTests.ps1 -CheckEnvironment
.\Run-NcdTests.ps1 -RepairEnvironment
```

## Why This Is Safer

### The Problem with Batch Files

```batch
:: In a batch file, this cleanup NEVER runs on Ctrl+C
setlocal
set "LOCALAPPDATA=%TEMP%\ncd_test_12345"
:: ... do tests ...
endlocal  :: <-- This is skipped on Ctrl+C!
```

### The PowerShell Solution

```powershell
# In PowerShell, this cleanup ALWAYS runs, even on Ctrl+C
try {
    $env:LOCALAPPDATA = "$env:TEMP\ncd_test_12345"
    # ... do tests ...
}
finally {
    # This block ALWAYS executes, even on Ctrl+C!
    Restore-NcdTestEnvironment
    Stop-NcdProcesses
    Remove-TestTempDirs
}
```

## Environment Protection

The PowerShell runner protects your environment in several ways:

### 1. Automatic Environment Save/Restore

Before running any tests:
- Saves `LOCALAPPDATA`
- Saves `NCD_TEST_MODE`
- Saves `TEMP`, `PATH`, `XDG_DATA_HOME`

After tests (even on Ctrl+C):
- Restores all saved variables
- Verifies restoration succeeded

### 2. Orphaned Directory Cleanup

Automatically removes:
- `ncd_test_*` directories
- `ncd_*_test_*` directories
- `ncd_unit_test_*` directories
- `ncd_master_test_*` directories

### 3. Process Cleanup

Automatically stops:
- `NCDService.exe` processes
- WSL `ncd_service` processes

### 4. VHD Management

When using VHD-based tests:
- Tracks attached VHDs
- Detaches them on cleanup
- Removes VHD files

## Using the PowerShell Module

For custom test scripts, import the module:

```powershell
Import-Module .\test\PowerShell\NcdTestUtils.psm1

# Save environment
Save-NcdTestEnvironment

# Run your tests
& .\my-test.bat

# Restore environment (even on error)
Restore-NcdTestEnvironment
```

### Available Functions

| Function | Purpose |
|----------|---------|
| `Save-NcdTestEnvironment` | Saves current environment state |
| `Restore-NcdTestEnvironment` | Restores saved environment |
| `Invoke-NcdTest` | Runs tests with guaranteed cleanup |
| `Invoke-NcdTestBatch` | Runs batch files with cleanup |
| `Test-NcdEnvironment` | Checks if environment is clean |
| `Repair-NcdEnvironment` | Fixes corrupted environment |
| `New-NcdTestVhd` | Creates test VHD with cleanup |
| `Remove-NcdTestVhd` | Removes VHD safely |
| `Stop-NcdService` | Stops all NCD services |

## Environment Check and Repair

### Check Only

```powershell
.\Check-Environment.ps1
```

Checks:
- `LOCALAPPDATA` for corruption
- `NCD_TEST_MODE` setting
- NCD database location
- Orphaned test directories
- Running NCD processes

### Check and Repair

```powershell
.\Check-Environment.ps1 -Repair
```

Repairs:
- Resets `LOCALAPPDATA` to correct path
- Clears `NCD_TEST_MODE` if set
- Removes orphaned directories
- Stops lingering processes

## Migration from Batch Files

### Old Way (Risky)

```batch
:: Cleanup might not run!
test\Test-Service-Windows.bat
```

### New Way (Safe)

```batch
:: Cleanup guaranteed!
Run-Tests-Safe.bat service
```

Or directly in PowerShell:

```powershell
# Cleanup guaranteed!
.\Run-NcdTests.ps1 -TestSuite Service
```

## Troubleshooting

### "Execution of scripts is disabled"

Run PowerShell as Administrator and execute:

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

Or use the batch wrapper which bypasses the policy for this script.

### Tests still leaving orphaned directories

Check what's happening:

```powershell
.\Check-Environment.ps1 -Verbose
```

Force cleanup:

```powershell
.\Check-Environment.ps1 -Repair
```

### Want to see detailed cleanup

```powershell
.\Run-NcdTests.ps1 -VerboseOutput
```

## Technical Details

### How Ctrl+C Trapping Works

PowerShell runs in the same console as batch files but handles interrupts differently:

1. When Ctrl+C is pressed, PowerShell sets a flag
2. The current command completes or times out
3. The `finally` block executes
4. Cleanup runs
5. Then the script exits

### Why Batch Files Can't Do This

Batch files use `cmd.exe` which:
- Has no exception handling
- Has no `finally` equivalent
- Terminates immediately on Ctrl+C
- Cannot register signal handlers

### Why PowerShell Can

PowerShell:
- Has full exception handling (`try/catch/finally`)
- Runs on .NET which handles signals
- Can register for cleanup callbacks
- Continues to `finally` blocks even on Ctrl+C

## Summary

| Feature | Batch Files | PowerShell |
|---------|-------------|------------|
| Ctrl+C trapping | ❌ No | ✅ Yes |
| Guaranteed cleanup | ❌ No | ✅ Yes |
| Environment restore | ❌ Unreliable | ✅ Guaranteed |
| Orphaned dir cleanup | ❌ Manual | ✅ Automatic |
| Process cleanup | ❌ Manual | ✅ Automatic |
| VHD management | ⚠️ Partial | ✅ Full |
| Detailed diagnostics | ❌ Limited | ✅ Comprehensive |

**Recommendation**: Use `Run-Tests-Safe.bat` or `Run-NcdTests.ps1` for all test runs.
