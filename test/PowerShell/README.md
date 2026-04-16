# PowerShell Test Utilities

This directory contains PowerShell modules and scripts for safer NCD testing.

## Why PowerShell?

Batch files **cannot** trap Ctrl+C interrupts. When you press Ctrl+C:
- Batch files terminate immediately
- Cleanup code never runs
- Environment variables stay modified

PowerShell's `try/finally` blocks **CAN** trap Ctrl+C, ensuring cleanup always runs.

## Quick Reference

### Run Tests Safely

```batch
:: From project root
Run-Tests-Safe.bat
```

### Check Environment

```powershell
# Check only
.\Check-Environment.ps1

# Check and repair
.\Check-Environment.ps1 -Repair
```

### Use in Your Scripts

```powershell
Import-Module .\test\PowerShell\NcdTestUtils.psm1

# Save environment
Save-NcdTestEnvironment

# Register cleanup actions
Register-NcdCleanupAction -Description "Remove temp files" -Action {
    Remove-Item $tempFiles -Recurse
}

# Your test code here...

# Cleanup happens automatically, even on Ctrl+C!
```

## Files

| File | Description |
|------|-------------|
| `NcdTestUtils.psm1` | Main module with cleanup utilities |
| `README.md` | This file |

## See Also

- `docs/POWERHSHELL_TEST_RUNNER.md` - Full documentation
- `..\..\Run-Tests-Safe.bat` - Safe test runner
- `..\..\Run-NcdTests.ps1` - PowerShell test runner
- `..\..\Check-Environment.ps1` - Environment diagnostics
