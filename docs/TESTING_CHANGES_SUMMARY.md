# Testing Infrastructure Changes Summary

## Overview

This document summarizes the changes made to improve NCD's test infrastructure, specifically addressing the issue of cleanup when tests are interrupted.

## The Problem

**Batch files cannot trap Ctrl+C interrupts.** When a user pressed Ctrl+C during test execution:
- The batch file terminated immediately
- No cleanup code ran
- `endlocal` was never reached
- Environment variables remained modified (especially `LOCALAPPDATA`)
- User's NCD appeared "broken" after tests

## The Solution

Created a **PowerShell-based test infrastructure** that uses `try/finally` blocks which **CAN** trap Ctrl+C and guarantee cleanup.

---

## New Files Created

### 1. PowerShell Test Runner

**File:** `Run-NcdTests.ps1` (18KB)
- Main PowerShell test runner
- Guarantees cleanup via `try/finally` blocks
- Supports all test suites (Unit, Integration, Service, etc.)
- Automatic environment save/restore
- Process cleanup (NCDService.exe)
- Orphaned directory cleanup

**Usage:**
```powershell
.\Run-NcdTests.ps1
.\Run-NcdTests.ps1 -TestSuite Unit
.\Run-NcdTests.ps1 -CheckEnvironment
.\Run-NcdTests.ps1 -RepairEnvironment
```

### 2. Safe Batch Wrapper

**File:** `Run-Tests-Safe.bat` (6KB)
- Batch wrapper for users who prefer starting from batch
- Calls PowerShell script with proper parameters
- Provides familiar command-line interface

**Usage:**
```batch
Run-Tests-Safe.bat
Run-Tests-Safe.bat unit
Run-Tests-Safe.bat --check
Run-Tests-Safe.bat --repair
```

### 3. Environment Diagnostics

**File:** `Check-Environment.ps1` (13KB)
- Comprehensive environment checking
- Automatic repair of corrupted environments
- Detects corrupted LOCALAPPDATA
- Finds orphaned test directories
- Identifies running NCD processes

**Usage:**
```powershell
.\Check-Environment.ps1
.\Check-Environment.ps1 -Repair
.\Check-Environment.ps1 -Verbose
```

### 4. PowerShell Module

**File:** `test/PowerShell/NcdTestUtils.psm1` (17KB)
- Reusable PowerShell module for test utilities
- Functions for environment management
- Cleanup action registration
- VHD management utilities

**Key Functions:**
- `Save-NcdTestEnvironment` / `Restore-NcdTestEnvironment`
- `Invoke-NcdTest` - Run tests with guaranteed cleanup
- `Register-NcdCleanupAction` - Register custom cleanup
- `Test-NcdEnvironment` / `Repair-NcdEnvironment`
- `New-NcdTestVhd` / `Remove-NcdTestVhd`
- `Stop-NcdService`

### 5. Documentation

**Files:**
- `docs/POWERHSHELL_TEST_RUNNER.md` - Full PowerShell runner documentation
- `test/PowerShell/README.md` - Module documentation
- `test_cleanup_solution.md` - Solution technical details
- `docs/TESTING_CHANGES_SUMMARY.md` - This file

---

## Updated Files

### 1. test/README.md

**Changes:**
- Added Quick Start section with PowerShell runner
- Added "How Tests Work" section explaining PowerShell advantages
- Added "How to Add New Tests" section with examples
- Added comprehensive troubleshooting guide
- Organized content with table of contents
- Added test execution flow documentation

### 2. AGENTS.md

**Changes:**
- Added "Quick Start (Recommended: PowerShell Safe Runner)" section at top of Testing section
- Updated Test Runner Reference table to include PowerShell runners
- Enhanced Test Environment Variables section with safety comparison
- Added Troubleshooting section with common issues and fixes

### 3. test/Check-Environment.bat

**Changes:**
- Added deprecation notice
- Now forwards to PowerShell version if available
- Falls back to legacy batch implementation

---

## Key Features

### Safety Guarantees

| Feature | Batch Files | PowerShell |
|---------|-------------|------------|
| Ctrl+C trapping | ❌ No | ✅ Yes |
| Guaranteed cleanup | ❌ No | ✅ Yes |
| Environment restore | ❌ Unreliable | ✅ Guaranteed |
| Orphaned dir cleanup | ❌ Manual | ✅ Automatic |
| Process cleanup | ❌ Manual | ✅ Automatic |

### How It Works

1. **Save Environment** - Store LOCALAPPDATA, NCD_TEST_MODE, TEMP, PATH
2. **Set Test Mode** - Enable NCD_TEST_MODE=1 (disables background rescans)
3. **Create Isolation** - Set LOCALAPPDATA to temp directory
4. **Run Tests** - Execute the test suite
5. **Cleanup (Always)** - Restore environment, stop processes, remove temp files

### PowerShell Advantage

```powershell
# This cleanup ALWAYS runs, even on Ctrl+C
try {
    Save-NcdTestEnvironment
    $env:LOCALAPPDATA = "$env:TEMP\ncd_test_12345"
    # ... run tests ...
}
finally {
    # This ALWAYS executes, even on Ctrl+C!
    Restore-NcdTestEnvironment
    Stop-NcdProcesses
    Remove-TestTempDirs
}
```

---

## Usage Examples

### Run All Tests (Safest)

```batch
Run-Tests-Safe.bat
```

### Run Specific Test Suites

```batch
Run-Tests-Safe.bat unit
Run-Tests-Safe.bat integration
Run-Tests-Safe.bat service
Run-Tests-Safe.bat windows
Run-Tests-Safe.bat wsl
```

### Check and Repair Environment

```batch
:: Check only
Run-Tests-Safe.bat --check

:: Check and repair
Run-Tests-Safe.bat --repair
```

### PowerShell Native

```powershell
# Run all tests
.\Run-NcdTests.ps1

# Run with options
.\Run-NcdTests.ps1 -TestSuite Unit -Quick
.\Run-NcdTests.ps1 -TestSuite Integration -WindowsOnly
```

---

## How to Add New Tests

### Unit Tests (C Code)

1. Choose appropriate test file (`test_database.c`, `test_matcher.c`, etc.)
2. Write test using framework:
```c
TEST(my_new_test) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    // ... test logic ...
    db_free(db);
    return 0;
}
```
3. Register in test suite: `RUN_TEST(my_new_test);`

### Integration Tests (PowerShell)

```powershell
Import-Module .\test\PowerShell\NcdTestUtils.psm1

Invoke-NcdTest -TestName "My Test" -TestScript {
    & .\my-test.bat
    if ($LASTEXITCODE -ne 0) { throw "Failed" }
}
# Cleanup happens automatically!
```

---

## Troubleshooting

### "LOCALAPPDATA points to test temp directory"

**Fix:**
```batch
Run-Tests-Safe.bat --repair
```

### "Execution of scripts is disabled"

**Fix:**
```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

Or use the batch wrapper which bypasses the policy.

### Tests Leave Orphaned Directories

**Fix:**
```powershell
.\Check-Environment.ps1 -Repair
```

---

## Migration Guide

### From Batch to PowerShell

**Old way (risky):**
```batch
test\Test-Service-Windows.bat
```

**New way (safe):**
```batch
Run-Tests-Safe.bat service
```

### For Test Writers

When writing new tests, use the PowerShell module for guaranteed cleanup:

```powershell
Import-Module .\test\PowerShell\NcdTestUtils.psm1

Save-NcdTestEnvironment
Register-NcdCleanupAction -Description "Remove temp" -Action {
    Remove-Item $tempDir -Recurse
}
# ... your test ...
# Cleanup runs automatically!
```

---

## Benefits

1. **Safety**: Environment always restored, even on interrupt
2. **Convenience**: No manual cleanup needed
3. **Diagnostics**: Comprehensive environment checking
4. **Repair**: Automatic repair of common issues
5. **Backward Compatibility**: Existing batch files still work
6. **Extensibility**: PowerShell module for custom scripts

## Recommendation

**Use `Run-Tests-Safe.bat` or `Run-NcdTests.ps1` for all test runs.**

The batch files still work for backward compatibility, but the PowerShell runners provide superior safety guarantees.
